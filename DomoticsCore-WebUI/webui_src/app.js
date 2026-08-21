class DomoticsApp {
    constructor() {
        this.pollInterval = null;
        this.sseSource = null;
        this.uiSchema = [];
        this.isEditingDeviceName = false;
        this.editingContexts = new Set();

        // Enum mappings from C++ backend
        this.WebUILocation = { Dashboard: 0, ComponentDetail: 1, HeaderStatus: 2, QuickControls: 3, Settings: 4, HeaderInfo: 5 };
        this.WebUIFieldType = { Text: 0, Number: 1, Float: 2, Boolean: 3, Select: 4, Slider: 5, Color: 6, Button: 7, Display: 8, Chart: 9, Status: 10, Progress: 11, Password: 12, File: 13, Multiselect: 14, OrderedList: 15 };

        // Chart history storage (contextId_fieldName -> array of values)
        this.chartData = new Map();
        this.maxChartPoints = 60; // 2 minutes at 2s intervals

        this.init();
    }

    async init() {
        console.log('[DC] init start, readyState=' + document.readyState);
        this.setupEventListeners();
        // On memory-constrained devices (ESP8266), wait for page to fully load
        // before making any requests to avoid concurrent TCP connections.
        if (document.readyState !== 'complete') {
            await new Promise(resolve => window.addEventListener('load', resolve));
        }
        // Start polling immediately — schema is fetched through the poll endpoint
        // (?schema=1) to avoid a separate TCP connection that would fail during
        // the ~50s TIME_WAIT after the 14KB HTML response on ESP8266.
        console.log('[DC] load done, starting polling (schema via poll endpoint)');
        this.startPolling();
    }

    async loadUISchema() {
        try {
            // Fetch schema through the polling endpoint to reuse the same TCP connection
            const response = await fetch(`/api/ui/updates?schema=1&t=${Date.now()}`);
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            this.uiSchema = await response.json();
            
            // Apply theme and primary color from webui_settings
            const webuiSettings = this.uiSchema.find(ctx => ctx.contextId === 'webui_settings');
            if (webuiSettings && webuiSettings.fields) {
                const themeField = webuiSettings.fields.find(f => f.name === 'theme');
                if (themeField && themeField.value) {
                    this.applyTheme(themeField.value);
                }
                const colorField = webuiSettings.fields.find(f => f.name === 'primary_color');
                if (colorField && colorField.value) {
                    this.applyPrimaryColor(colorField.value);
                }
            }
            
            // Inject custom CSS and JS from components
            this.injectCustomStyles();
            this.injectCustomScripts();
        } catch (error) {
            console.error('Failed to load UI schema:', error);
            // You could render an error message to the user here
        }
    }

    renderUI() {
        this.renderSection(this.WebUILocation.Dashboard, 'dashboardGrid');
        this.renderSection(this.WebUILocation.ComponentDetail, 'componentsGrid'); // Using ComponentDetail for the 'Components' tab for now
        this.renderSection(this.WebUILocation.Settings, 'settingsGrid');
        this.renderHeaderStatus();
        this.renderHeaderInfo();
        this.renderFooter();
    }

    applyTheme(theme) {
        document.body.classList.remove('light', 'dark');
        if (theme === 'light') {
            document.body.classList.add('light');
        } else if (theme === 'dark') {
            document.body.classList.add('dark');
        } else {
            const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
            document.body.classList.add(prefersDark ? 'dark' : 'light');
        }
    }

    applyPrimaryColor(color) {
        if (color && color.startsWith('#')) {
            document.documentElement.style.setProperty('--primary-color', color);
        }
    }

    applyThemeFromSchema() {
        // Look for webui_settings context theme field default
        const settings = this.uiSchema.find(ctx => ctx.contextId === 'webui_settings');
        if (!settings) return;
        const themeField = settings.fields && settings.fields.find(f => f.name === 'theme');
        if (themeField && themeField.value) {
            this.applyTheme(themeField.value);
        }
    }

    renderSection(location, gridId) {
        const grid = document.getElementById(gridId);
        if (!grid) return;

        const contexts = this.uiSchema.filter(ctx => ctx.location === location);
        contexts.sort((a, b) => (b.priority || 0) - (a.priority || 0));

        if (contexts.length > 0) {
            grid.innerHTML = '';
            contexts.forEach(context => {
                const card = this.createContextCard(context);
                grid.appendChild(card);
            });
        } else {
            grid.innerHTML = '<div class="loading-message">No components for this section.</div>';
        }
    }

    createContextCard(context) {
        const card = document.createElement('div');
        card.className = 'card';
        card.dataset.contextId = context.contextId;

        // Only show Edit button if context has editable fields and is not always interactive
        const hasEditableFields = context.location === this.WebUILocation.Settings && 
                                   !context.alwaysInteractive &&
                                   Array.isArray(context.fields) && 
                                   context.fields.some(f => !f.readOnly);
        const actionsHtml = hasEditableFields ? `
                <div class="card-actions">
                    <button class="btn btn-edit" data-action="edit">Edit</button>
                    <button class="btn btn-save" data-action="save" style="display:none">Save</button>
                    <button class="btn btn-cancel" data-action="cancel" style="display:none">Cancel</button>
                </div>` : '';

        if (context.customHtml) {
            card.innerHTML = context.customHtml;
            if (context.location === this.WebUILocation.Settings) {
                let header = card.querySelector('.card-header');
                if (!header) {
                    header = document.createElement('div');
                    header.className = 'card-header';
                    header.innerHTML = `<h3 class="card-title">${context.title || ''}</h3>`;
                    card.insertBefore(header, card.firstChild);
                }
                if (!header.querySelector('.card-actions')) {
                    header.insertAdjacentHTML('beforeend', actionsHtml);
                }
                if (!card.querySelector('.card-content')) {
                    const contentWrapper = document.createElement('div');
                    contentWrapper.className = 'card-content';
                    const toMove = [];
                    card.childNodes.forEach(n => { if (n !== header) toMove.push(n); });
                    toMove.forEach(n => contentWrapper.appendChild(n));
                    card.appendChild(contentWrapper);
                }
            }
        } else {
            let fieldsHtml = '';
            context.fields.forEach(field => {
                fieldsHtml += this.renderField(field, context.contextId);
            });

            card.innerHTML = `
                <div class="card-header">
                    <h3 class="card-title">${context.title}</h3>
                    ${actionsHtml}
                </div>
                <div class="card-content">${fieldsHtml}</div>
            `;
        }

        if (context.location === this.WebUILocation.Settings && !context.alwaysInteractive) {
            card.dataset.editing = 'false';
            this.setCardEditing(card, false);
        }

        this.attachFieldEventListeners(card, context);
        this.attachCardActions(card, context);
        return card;
    }

    renderHeaderStatus() {
        const container = document.querySelector('.status-indicators');
        if (!container) return;
        // Clear all to match schema exactly (prevents stale badges after disable)
        container.innerHTML = '';
        const statuses = this.uiSchema
            .filter(ctx => ctx.location === this.WebUILocation.HeaderStatus)
            .sort((a, b) => (b.priority || 0) - (a.priority || 0));
        statuses.forEach(ctx => {
            const indicator = document.createElement('span');
            indicator.className = 'status-indicator';
            indicator.dataset.contextId = ctx.contextId;
            if (ctx.customHtml) {
                indicator.innerHTML = ctx.customHtml;
            } else {
                this._setBadgeIcon(indicator, ctx.icon || 'dc-components');
            }
            container.appendChild(indicator);
        });
    }

    renderHeaderInfo() {
        const container = document.querySelector('.header-info');
        if (!container) return;
        // Clear all to match schema exactly
        container.innerHTML = '';
        const infos = this.uiSchema
            .filter(ctx => ctx.location === this.WebUILocation.HeaderInfo)
            .sort((a, b) => (b.priority || 0) - (a.priority || 0));
        infos.forEach(ctx => {
            const infoItem = document.createElement('span');
            infoItem.className = 'header-info-item';
            infoItem.dataset.contextId = ctx.contextId;
            
            // Display first field value (time, uptime, etc.)
            const field = ctx.fields && ctx.fields.length > 0 ? ctx.fields[0] : null;
            if (field) {
                infoItem.innerHTML = `
                    <svg class="icon" viewBox="0 0 1024 1024"><use href="#${ctx.icon || 'dc-info'}"/></svg>
                    <span class="header-info-value" data-field="${field.name}">${field.value || '--'}</span>
                `;
            }
            container.appendChild(infoItem);
        });
    }

    renderFooter() {
        const footerText = document.getElementById('footerText');
        if (!footerText) return;
        
        // Find system_info to get device name, manufacturer, firmware version
        const systemInfo = this.uiSchema.find(ctx => ctx.contextId === 'system_info');
        let deviceName = 'DomoticsCore';
        let manufacturer = '';
        let firmwareVersion = '';
        
        if (systemInfo && systemInfo.fields) {
            const nameField = systemInfo.fields.find(f => f.name === 'device_name');
            const mfgField = systemInfo.fields.find(f => f.name === 'manufacturer');
            const versionField = systemInfo.fields.find(f => f.name === 'firmware_version');
            
            if (nameField) deviceName = nameField.value || deviceName;
            if (mfgField) manufacturer = mfgField.value || '';
            if (versionField) firmwareVersion = versionField.value || '';
        }
        
        const parts = [];
        if (deviceName) parts.push(deviceName);
        if (manufacturer) parts.push(`by ${manufacturer}`);
        if (firmwareVersion) parts.push(`v${firmwareVersion}`);
        
        footerText.textContent = parts.join(' • ');
    }

    updateFooterAndHeader(systemInfoData) {
        // Update header device name
        const deviceNameEl = document.getElementById('deviceName');
        if (!this.isEditingDeviceName && deviceNameEl && systemInfoData.device_name) {
            deviceNameEl.textContent = systemInfoData.device_name;
        }
        
        // Update footer
        const footerText = document.getElementById('footerText');
        if (footerText) {
            const parts = [];
            if (systemInfoData.device_name) parts.push(systemInfoData.device_name);
            if (systemInfoData.manufacturer) parts.push(`by ${systemInfoData.manufacturer}`);
            if (systemInfoData.firmware_version) parts.push(`v${systemInfoData.firmware_version}`);
            if (parts.length > 0) {
                footerText.textContent = parts.join(' • ');
            }
        }
    }

    renderField(field, contextId) {
        const fieldId = `${contextId}_${field.name}`;
        let fieldHtml = '';
        let rowClass = '';
        switch (field.type) {
            case this.WebUIFieldType.Boolean:
                fieldHtml = `
                    <label class="toggle-switch">
                        <input type="checkbox" id="${fieldId}" ${field.value === 'true' ? 'checked' : ''} ${field.readOnly ? 'disabled' : ''}>
                        <span class="slider"></span>
                    </label>`;
                break;
            case this.WebUIFieldType.Text:
                {
                    const inputType = field.name === 'password' ? 'password' : 'text';
                    const val = (field.value != null) ? String(field.value) : '';
                    fieldHtml = `<input type="${inputType}" id="${fieldId}" value="${val}" ${field.readOnly ? 'disabled' : ''}>`;
                }
                break;
            case this.WebUIFieldType.Password:
                {
                    const val = (field.value != null) ? String(field.value) : '';
                    fieldHtml = `<input type="password" id="${fieldId}" value="${val}" ${field.readOnly ? 'disabled' : ''}>`;
                }
                break;
            case this.WebUIFieldType.Number:
                {
                    const val = (field.value != null) ? String(field.value) : '';
                    fieldHtml = `<input type="number" id="${fieldId}" value="${val}" ${field.readOnly ? 'disabled' : ''}>`;
                }
                break;
            case this.WebUIFieldType.Float:
                {
                    const val = (field.value != null) ? String(field.value) : '';
                    fieldHtml = `<input type="number" step="any" id="${fieldId}" value="${val}" ${field.readOnly ? 'disabled' : ''}>`;
                }
                break;
            case this.WebUIFieldType.Select:
            case this.WebUIFieldType.Multiselect:
                {
                    const multiple = field.type === this.WebUIFieldType.Multiselect;
                    const current = multiple ? (Array.isArray(field.value) ? field.value : []) : String(field.value || '');
                    const selectedValues = new Set(multiple ? current.map(String) : [current]);
                    const selectAttributes = multiple ? 'multiple size="5"' : '';
                    // Check if options should be loaded from endpoint
                    if (field.endpoint && field.endpoint.length > 0) {
                        // Create select with loading placeholder, fetch options async
                        fieldHtml = `<select id="${fieldId}" ${selectAttributes} ${field.readOnly ? 'disabled' : ''} data-endpoint="${field.endpoint}"><option value="">Loading...</option></select>`;
                        // Queue async load after render
                        setTimeout(() => this.loadSelectOptions(fieldId, field.endpoint, current), 0);
                    } else {
                        const options = (field.options || []).length ? field.options : (typeof field.unit === 'string' && field.unit.includes(',')) ? field.unit.split(',') : [];
                        const labels = field.optionLabels || {};
                        const optsHtml = options.map(opt => {
                            const sel = selectedValues.has(String(opt)) ? 'selected' : '';
                            const label = labels[opt] || opt;  // Use label if available, fallback to value
                            return `<option value="${opt}" ${sel}>${label}</option>`;
                        }).join('');
                        fieldHtml = `<select id="${fieldId}" ${selectAttributes} ${field.readOnly ? 'disabled' : ''}>${optsHtml}</select>`;
                    }
                    if (multiple) rowClass = 'field-row-multiselect';
                }
                break;
            case this.WebUIFieldType.OrderedList:
                fieldHtml = `<div id="${fieldId}" class="ordered-list">${this.orderedListItemsHtml(field.value, field.optionLabels)}</div>`;
                rowClass = 'field-row-ordered-list';
                break;
            case this.WebUIFieldType.Slider:
                {
                    const min = (field.minValue !== undefined && field.minValue !== null) ? field.minValue : 0;
                    const max = (field.maxValue !== undefined && field.maxValue !== null) ? field.maxValue : 255;
                    fieldHtml = `<input type="range" id="${fieldId}" min="${min}" max="${max}" value="${field.value}" ${field.readOnly ? 'disabled' : ''}>`;
                }
                 break;
            case this.WebUIFieldType.Color:
                {
                    const val = (field.value != null) ? String(field.value) : '#000000';
                    fieldHtml = `<input type="color" id="${fieldId}" value="${val}" ${field.readOnly ? 'disabled' : ''}>`;
                }
                break;
            case this.WebUIFieldType.Button:
                fieldHtml = `<button class="btn" id="${fieldId}" ${field.readOnly ? 'disabled' : ''}>${field.label}</button>`;
                break;
            case this.WebUIFieldType.Status:
                {
                    // Status field handling (generic)
                    fieldHtml = `<span class="card-status status-success" id="${fieldId}">${field.value}</span>`;
                }
                break;
            case this.WebUIFieldType.Progress:
                {
                    // field.value might be "50%" or just "50"
                    let percent = parseFloat(field.value) || 0;
                    fieldHtml = `
                        <div class="progress-bar-container">
                            <div class="progress-bar-fill" id="${fieldId}" style="width: ${percent}%"></div>
                        </div>
                        <div id="${fieldId}_text" class="progress-text">${field.value || '0%'}</div>
                    `;
                }
                break;
            case this.WebUIFieldType.File:
                fieldHtml = `
                    <div class="file-upload-container">
                        <input type="file" id="${fieldId}" accept="${field.unit || ''}" style="display:none;" ${field.readOnly ? 'disabled' : ''}>
                        <div class="file-select-row">
                            <button class="btn btn-secondary" id="${fieldId}_select" ${field.readOnly ? 'disabled' : ''}>Select File...</button>
                            <span id="${fieldId}_filename" class="file-name">No file selected</span>
                        </div>
                        <button class="btn btn-primary file-upload-btn" id="${fieldId}_btn" disabled>Upload</button>
                        <div id="${fieldId}_status" class="file-status"></div>
                    </div>
                `;
                break;
            case this.WebUIFieldType.Chart:
                // Line chart with canvas
                fieldHtml = `
                    <div class="chart-container">
                        <canvas id="${fieldId}_canvas" width="300" height="120"></canvas>
                        <div class="chart-value" data-field-name="${field.name}">
                            <span class="value" id="${fieldId}_value">0${field.unit || ''}</span>
                        </div>
                    </div>`;
                break;
            case this.WebUIFieldType.Display:
            default:
                // Add a data-attribute to uniquely identify the value span for WS updates
                fieldHtml = `<span class="field-value" data-field-name="${field.name}">${field.value} ${field.unit || ''}</span>`;
                break;
        }

        return `
            <div class="field-row ${rowClass}">
                <span class="field-label">${field.label}:</span>
                ${fieldHtml}
            </div>`;
    }

    async loadSelectOptions(fieldId, endpoint, currentValue) {
        try {
            const response = await fetch(endpoint);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const options = await response.json();
            
            const select = document.getElementById(fieldId);
            if (!select) return;
            
            // Build options HTML - expects [{value, label}] format
            const selectedValues = new Set(select.multiple ? (Array.isArray(currentValue) ? currentValue.map(String) : []) : [String(currentValue)]);
            const optsHtml = options.map(opt => {
                const val = opt.value || opt;
                const label = opt.label || val;
                const sel = selectedValues.has(String(val)) ? 'selected' : '';
                return `<option value="${val}" ${sel}>${label}</option>`;
            }).join('');
            
            select.innerHTML = optsHtml;
        } catch (err) {
            console.error(`Failed to load options from ${endpoint}:`, err);
            const select = document.getElementById(fieldId);
            if (select) select.innerHTML = '<option value="">Error loading options</option>';
        }
    }

    orderedListItemsHtml(items, labels = {}) {
        if (!Array.isArray(items)) return '';
        return items.map(item => {
            const value = String(item == null ? '' : item);
            const label = String(labels && labels[value] != null ? labels[value] : value);
            return `<div class="ordered-list-item" draggable="false" data-value="${value}"><span class="drag-handle">&#8942;&#8942;</span><span>${label}</span></div>`;
        }).join('');
    }

    renderOrderedListItems(element, items, labels = {}) {
        if (element) element.innerHTML = this.orderedListItemsHtml(items, labels);
    }

    getOrderedListValue(element) {
        if (!element) return [];
        return Array.from(element.querySelectorAll('.ordered-list-item'))
            .map(item => item.dataset.value || '');
    }

    attachOrderedList(card, context, field, element) {
        let dragged = null;
        element.addEventListener('dragstart', event => {
            if (card.dataset.editing !== 'true') {
                event.preventDefault();
                return;
            }
            dragged = event.target.closest('.ordered-list-item');
            if (dragged) dragged.classList.add('dragging');
        });
        element.addEventListener('dragover', event => {
            if (!dragged || card.dataset.editing !== 'true') return;
            event.preventDefault();
            const target = event.target.closest('.ordered-list-item');
            if (!target || target === dragged) return;
            const after = event.clientY > target.getBoundingClientRect().top + target.offsetHeight / 2;
            element.insertBefore(dragged, after ? target.nextSibling : target);
        });
        element.addEventListener('dragend', () => {
            if (!dragged) return;
            dragged.classList.remove('dragging');
            dragged = null;
            if (card.dataset.editing === 'true') {
                this.bufferFieldChange(card, field.name, this.getOrderedListValue(element));
            }
        });
    }

    setupEventListeners() {
        const menuToggle = document.getElementById('menuToggle');
        const sidebar = document.getElementById('sidebar');
        const navLinks = document.querySelectorAll('.nav-link');

        if (menuToggle && sidebar) {
            menuToggle.addEventListener('click', () => sidebar.classList.toggle('open'));
        }

        navLinks.forEach(link => {
            link.addEventListener('click', e => {
                e.preventDefault();
                this.showSection(link.dataset.section);
                navLinks.forEach(l => l.classList.remove('active'));
                link.classList.add('active');
            });
        });

        document.addEventListener('click', e => {
            if (sidebar && menuToggle && !sidebar.contains(e.target) && !menuToggle.contains(e.target)) {
                sidebar.classList.remove('open');
            }
        });

        const deviceNameEl = document.getElementById('deviceName');
        if (deviceNameEl) {
            deviceNameEl.addEventListener('click', () => this.makeDeviceNameEditable(deviceNameEl));
        }
    }

    startPolling() {
        if (this.pollInterval) return;
        console.log('[DC] Polling started (2s interval)');
        this.pollInterval = setInterval(async () => {
            try {
                // If schema not loaded yet, fetch it first via the same endpoint
                if (this.uiSchema.length === 0) {
                    console.log('[DC] Loading schema via poll endpoint...');
                    await this.loadUISchema();
                    if (this.uiSchema.length > 0) {
                        try {
                            this.applyThemeFromSchema();
                            this.renderUI();
                        } catch(e) {
                            console.error('renderUI failed:', e);
                        }
                    }
                    return; // Skip normal poll this tick, schema response is enough
                }
                // If SSE is active, no need to poll for updates
                if (this.sseSource) return;
                const resp = await fetch('/api/ui/updates');
                if (resp.ok) {
                    const data = await resp.json();
                    // Check for SSE upgrade hint from server
                    if (data._sse && !this.sseSource) {
                        this.connectSSE(data._sse);
                    }
                    this.handleWebSocketMessage(data);
                }
            } catch (e) {
                // Network error, retry next interval
            }
        }, 2000);
    }

    stopPolling() {
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
            console.log('[DC] Polling stopped');
        }
    }

    connectSSE(url) {
        if (this.sseSource) return;
        console.log('[DC] Connecting SSE to', url);
        const es = new EventSource(url);
        es.addEventListener('message', (event) => {
            try {
                const data = JSON.parse(event.data);
                this.handleWebSocketMessage(data);
            } catch (e) {
                console.error('[DC] SSE parse error:', e);
            }
        });
        es.addEventListener('open', () => {
            console.log('[DC] SSE connected — stopping polling');
            this.sseSource = es;
            // Keep pollInterval alive but it will skip updates (sseSource check)
        });
        es.addEventListener('error', () => {
            if (es.readyState === EventSource.CLOSED) {
                console.log('[DC] SSE closed — falling back to polling');
                this.sseSource = null;
                es.close();
            }
        });
    }

    stopSSE() {
        if (this.sseSource) {
            this.sseSource.close();
            this.sseSource = null;
            console.log('[DC] SSE stopped');
        }
    }

    handleWebSocketMessage(data) {
        // WiFi network change: force immediate reconnect (e.g., AP -> STA switch)
        if (data && data.type === 'wifi_network_changed') {
            console.log('[DC] WiFi network changed - restarting polling...');
            this.stopPolling();
            setTimeout(() => {
                this.startPolling();
            }, 1000);
            return;
        }
        
        // Schema change notification from server: re-fetch schema and re-render
        if (data && data.type === 'schema_changed') {
            this.loadUISchema().then(() => {
                this.renderUI();
                // If the Components tab is currently active, refresh it as well
                const compSection = document.getElementById('components-section');
                if (compSection && compSection.classList.contains('active')) {
                    this.loadComponents();
                }
            });
            return;
        }
        if (data.system) {
            this.updateSystemInfo(data.system);
            this.updateSystemOverviewCard(data.system);
        }
        if (data.contexts) {
            this.updateDashboard(data.contexts);
            this.updateCharts(data.contexts);
        }
    }

    updateSystemOverviewCard(system) {
        const card = document.querySelector(`.card[data-context-id='system_overview']`);
        if (!card) return;

        const uptimeEl = card.querySelector(`[data-field-name='uptime']`);
        if (uptimeEl) {
            const uptime = Math.floor((Date.now() - this.systemStartTime) / 1000);
            const hours = Math.floor(uptime / 3600).toString().padStart(2, '0');
            const minutes = Math.floor((uptime % 3600) / 60).toString().padStart(2, '0');
            const seconds = (uptime % 60).toString().padStart(2, '0');
            uptimeEl.textContent = `${hours}:${minutes}:${seconds}`;
        }

        const heapEl = card.querySelector(`[data-field-name='heap']`);
        if (heapEl && typeof system.heap === 'number') {
            heapEl.textContent = `${(system.heap / 1024).toFixed(1)} KB`;
        }
    }

    updateSystemInfo(system) {
        if (typeof system.uptime === 'number') {
            this.systemStartTime = Date.now() - system.uptime;
        }
        const deviceNameEl = document.getElementById('deviceName');
        if (!this.isEditingDeviceName && deviceNameEl && system.device_name) {
            deviceNameEl.textContent = system.device_name;
        }

        const footerTextEl = document.getElementById('footerText');
        if (footerTextEl && system.manufacturer && system.version) {
            footerTextEl.textContent = `${system.manufacturer} ${system.version} | ESP32 Framework`;
        }
    }

    updateDashboard(contexts) {
        Object.entries(contexts).forEach(([contextId, data]) => {
            const contextSchema = this.uiSchema.find(ctx => ctx.contextId === contextId);
            if (!contextSchema) return;

            // Update footer when system_info changes
            if (contextId === 'system_info' && data) {
                this.updateFooterAndHeader(data);
            }

            if (contextSchema.location === this.WebUILocation.HeaderStatus) {
                this.updateHeaderStatus(contextId, data);
            } else if (contextSchema.location === this.WebUILocation.HeaderInfo) {
                this.updateHeaderInfo(contextId, data);
            } else {
                const card = document.querySelector(`.card[data-context-id='${contextId}']`);
                if (card) {
                    if (card.dataset.editing === 'true') {
                        return;
                    }
                    Object.entries(data).forEach(([fieldName, value]) => {
                        const fieldSchema = Array.isArray(contextSchema.fields) ? contextSchema.fields.find(f => f.name === fieldName) : null;
                        
                        // Special handling for Progress fields
                        if (fieldSchema && fieldSchema.type === this.WebUIFieldType.Progress) {
                            const fieldId = `${contextId}_${fieldName}`;
                            const barEl = card.querySelector(`#${fieldId}`);
                            const textEl = card.querySelector(`#${fieldId}_text`);
                            if (barEl) {
                                let percent = parseFloat(value) || 0;
                                barEl.style.width = `${percent}%`;
                            }
                            if (textEl) {
                                textEl.textContent = value || '0%';
                            }
                            return;
                        }

                        // Special handling for Chart fields
                        if (fieldSchema && fieldSchema.type === this.WebUIFieldType.Chart) {
                            const fieldId = `${contextId}_${fieldName}`;
                            const numValue = parseFloat(value);
                            if (!isNaN(numValue)) {
                                this.updateChart(fieldId, numValue, fieldSchema.unit || '');
                            }
                            return;
                        }

                        // Try to find an input element first (for toggles, sliders, etc.)
                        const fieldId = `${contextId}_${fieldName}`;
                        const inputEl = card.querySelector(`#${fieldId}`);
                        if (inputEl) {
                            if (fieldSchema && fieldSchema.type === this.WebUIFieldType.OrderedList) {
                                this.renderOrderedListItems(inputEl, value, fieldSchema.optionLabels);
                            } else if (inputEl.type === 'checkbox') {
                                const newChecked = (value === 'true' || value === true);
                                if (inputEl.checked !== newChecked) {
                                    inputEl.checked = newChecked;
                                    // Trigger UI update listeners (e.g., custom JS updating bulbs)
                                    inputEl.dispatchEvent(new Event('change', { bubbles: true }));
                                }
                            } else if (inputEl.multiple) {
                                const selectedValues = new Set(Array.isArray(value) ? value.map(String) : []);
                                Array.from(inputEl.options).forEach(option => {
                                    option.selected = selectedValues.has(option.value);
                                });
                            } else {
                                // Do not overwrite if user is currently editing this input
                                if (document.activeElement === inputEl) return;
                                if (inputEl.value !== String(value)) {
                                    inputEl.value = value;
                                    // For sliders or other inputs, also trigger change to refresh visuals
                                    inputEl.dispatchEvent(new Event('change', { bubbles: true }));
                                }
                            }
                        } else {
                            // Otherwise, find a display span
                            const displayEl = card.querySelector(`[data-field-name='${fieldName}']`);
                            if (displayEl) {
                                const unit = fieldSchema && fieldSchema.unit ? fieldSchema.unit : '';
                                displayEl.textContent = this.formatValue(value) + unit;
                            }
                        }
                    });
                }
            }

            const cardEl = document.querySelector(`.card[data-context-id='${contextId}']`);
            if (!(cardEl && cardEl.dataset.editing === 'true')) {
                document.dispatchEvent(new CustomEvent('component-status-update', {
                    detail: { contextId, data }
                }));
            }
        });
    }

    updateHeaderInfo(contextId, data) {
        const container = document.querySelector('.header-info');
        if (!container) return;

        let infoItem = container.querySelector(`[data-context-id='${contextId}']`);
        if (!infoItem) return; // Item should already exist from renderHeaderInfo

        // Update field values
        Object.entries(data).forEach(([fieldName, value]) => {
            const valueEl = infoItem.querySelector(`[data-field='${fieldName}']`);
            if (valueEl) {
                valueEl.textContent = value || '--';
            }
        });
    }

    updateHeaderStatus(contextId, data) {
        const statusContainer = document.querySelector('.status-indicators');
        if (!statusContainer) return;

        let indicator = statusContainer.querySelector(`[data-context-id='${contextId}']`);
        if (!indicator) {
            const contextSchema = this.uiSchema.find(ctx => ctx.contextId === contextId);
            if (!contextSchema) return;

            indicator = document.createElement('span');
            indicator.className = 'status-indicator';
            indicator.dataset.contextId = contextId;
            // Use custom HTML if provided, otherwise clone icon from SVG sprite
            if (contextSchema.customHtml) {
                indicator.innerHTML = contextSchema.customHtml;
            } else {
                const iconId = data.icon || contextSchema.icon || 'dc-components';
                this._setBadgeIcon(indicator, iconId);
            }
            indicator.addEventListener('click', () => {
                // Find the settings context for this provider
                const providerId = contextSchema.contextId.split('_')[0];
                const settingsContext = this.uiSchema.find(ctx => 
                    ctx.contextId.startsWith(providerId) && ctx.location === this.WebUILocation.Settings
                );

                if (settingsContext) {
                    this.showSection('settings');
                    // Optional: scroll to the specific card
                    setTimeout(() => {
                        const card = document.querySelector(`.card[data-context-id='${settingsContext.contextId}']`);
                        if (card) card.scrollIntoView({ behavior: 'smooth', block: 'center' });
                    }, 100);
                }
            });
            statusContainer.appendChild(indicator);
        }

        // Update status based on data
        const state = data.state;
        indicator.classList.toggle('active', state === 'ON' || state === 'true' || state === true);
        
        // Dynamic icon switching: backend can provide "icon" field to change the badge icon
        if (data.icon) {
            this._setBadgeIcon(indicator, data.icon);
        }
        
        // Tooltip: use "tooltip" field if provided, otherwise show state
        indicator.title = data.tooltip || `${state || 'unknown'}`;
    }
    
    _setBadgeIcon(indicator, iconId) {
        const sym = document.getElementById(iconId);
        const vb = sym ? (sym.getAttribute('viewBox') || '0 0 24 24') : '0 0 24 24';
        const inner = sym ? sym.innerHTML : '<use href="#' + iconId + '"/>';
        indicator.innerHTML = '<svg class="icon" viewBox="' + vb + '">' + inner + '</svg>';
    }

    formatValue(value) {
        if (typeof value === 'boolean') return value ? 'On' : 'Off';
        if (typeof value === 'number' && value > 1_000_000) return (value / 1_000_000).toFixed(1) + 'M';
        if (typeof value === 'number' && value > 1_000) return (value / 1_000).toFixed(1) + 'K';
        return String(value);
    }


    showSection(sectionName) {
        document.querySelectorAll('.content-section').forEach(section => section.classList.remove('active'));
        const targetSection = document.getElementById(`${sectionName}-section`);
        if (targetSection) targetSection.classList.add('active');

        if (sectionName === 'components') {
            this.loadComponents();
        }
    }

    async loadComponents() {
        const grid = document.getElementById('componentsGrid');
        if (!grid) return;

        // ComponentDetail cards are rendered by renderUI(). Re-rendering them
        // here restores schema defaults after SSE has applied live values. Since
        // SSE sends deltas, unchanged contexts would then remain stale forever.
        let card = grid.querySelector('.components-card');
        if (!card) {
            card = document.createElement('div');
            card.className = 'card components-card';
            grid.appendChild(card);
        }
        card.innerHTML = '<div class="card-content"><div class="loading-message">Loading components...</div></div>';

        try {
            const response = await fetch('/api/components');
            const data = await response.json();
            
            if (data.components && data.components.length > 0) {
                let fieldsHtml = '<div class="components-list">';
                data.components.forEach(comp => {
                    const isWebUI = comp.name === 'WebUI';
                    const checked = comp.enabled ? 'checked' : '';
                    const statusClass = comp.enabled ? 'status-success' : 'status-warning';
                    fieldsHtml += `
                        <div class="field-row comp-row" data-comp-name="${comp.name}">
                            <div class="comp-col comp-name">${comp.name} <span class="comp-version">v${comp.version}</span></div>
                            <div class="comp-col comp-status"><span class="field-value ${statusClass}">${comp.enabled ? 'Enabled' : 'Disabled'}</span></div>
                            <div class="comp-col comp-toggle">
                                <label class="toggle-switch" title="Enable/Disable">
                                    <input type="checkbox" class="comp-toggle" ${checked}>
                                    <span class="slider"></span>
                                </label>
                            </div>
                        </div>`;
                });
                fieldsHtml += '</div>';
                card.innerHTML = `
                    <div class="card-header">
                        <h3 class="card-title">Registered Components</h3>
                    </div>
                    <div class="card-content">${fieldsHtml}</div>
                `;

                // Attach listeners for component enable/disable toggles
                card.querySelectorAll('.field-row').forEach(row => {
                    const name = row.dataset.compName;
                    const toggle = row.querySelector('.comp-toggle');
                    const valueEl = row.querySelector('.field-value');
                    if (!toggle) return;
                    toggle.addEventListener('change', async (e) => {
                        // Confirmation for disabling WebUI
                        if (name === 'WebUI' && e.target.checked === false) {
                            const ok = window.confirm('Disabling WebUI may make the UI inaccessible until reboot/reset. Continue?');
                            if (!ok) {
                                e.target.checked = true; // revert
                                return;
                            }
                        }
                        // Confirmation for disabling Wifi
                        if (name === 'Wifi' && e.target.checked === false) {
                            const ok = window.confirm('Disabling Wifi will stop network connectivity (except AP if enabled). Continue?');
                            if (!ok) {
                                e.target.checked = true;
                                return;
                            }
                        }
                        const enabled = e.target.checked;
                        try {
                            const body = new URLSearchParams({ name, enabled: String(enabled) }).toString();
                            const resp = await fetch('/api/components/enable', {
                                method: 'POST',
                                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                                body
                            });
                            const result = await resp.json();
                            if (!result.success) {
                                throw new Error('Operation failed');
                            }
                            // Update row status text
                            valueEl.textContent = enabled ? 'Enabled' : 'Disabled';
                            valueEl.classList.toggle('status-success', enabled);
                            valueEl.classList.toggle('status-warning', !enabled);
                            // Refresh schema and re-render UI to reflect contexts
                            await this.loadUISchema();
                            this.renderUI();
                        } catch (err) {
                            console.error('Failed to change component state:', err);
                            // Revert toggle on failure
                            e.target.checked = !enabled;
                            alert('Failed to change component state');
                        }
                    });
                });
            } else {
                card.innerHTML = '<div class="card-content"><div class="loading-message">No components registered.</div></div>';
            }
        } catch (error) {
            console.error('Failed to load components:', error);
            card.innerHTML = '<div class="card-content"><div class="loading-message">Error loading components.</div></div>';
        }
    }

    attachFieldEventListeners(card, context) {
        if (!Array.isArray(context.fields) || context.fields.length === 0) return;
        context.fields.forEach(field => {
            if (field.readOnly) return;

            const fieldId = `${context.contextId}_${field.name}`;

            if (field.type === this.WebUIFieldType.OrderedList) {
                const list = card.querySelector(`#${fieldId}`);
                if (list) this.attachOrderedList(card, context, field, list);
                return;
            }
            
            // Special handling for File upload fields
            if (field.type === this.WebUIFieldType.File) {
                const btn = card.querySelector(`#${fieldId}_btn`);
                const selectBtn = card.querySelector(`#${fieldId}_select`);
                const input = card.querySelector(`#${fieldId}`);
                const status = card.querySelector(`#${fieldId}_status`);
                const filenameEl = card.querySelector(`#${fieldId}_filename`);
                
                if (input) {
                    if (selectBtn) {
                        selectBtn.addEventListener('click', () => input.click());
                    }

                    input.addEventListener('change', () => {
                        if (input.files && input.files.length > 0) {
                            const file = input.files[0];
                            if (filenameEl) {
                                filenameEl.textContent = file.name + ' (' + (file.size / 1024 / 1024).toFixed(2) + ' MB)';
                                filenameEl.classList.add('has-file');
                            }
                            if (btn) btn.disabled = false;
                        } else {
                            if (filenameEl) {
                                filenameEl.textContent = 'No file selected';
                                filenameEl.classList.remove('has-file');
                            }
                            if (btn) btn.disabled = true;
                        }
                    });
                }
                
                if (btn && input) {
                    btn.addEventListener('click', async () => {
                        if (!input.files.length) {
                            if (status) status.textContent = 'Please select a file first.';
                            return;
                        }
                        const file = input.files[0];
                        const formData = new FormData();
                        // Use field name as the form key (OTA expects 'firmware')
                        formData.append(field.name, file);
                        
                        if (status) {
                            status.textContent = 'Uploading...';
                            status.className = 'file-status';
                        }
                        btn.disabled = true;
                        input.disabled = true;
                        if (selectBtn) selectBtn.disabled = true;
                        
                        try {
                            // Endpoint is in field.endpoint
                            const endpoint = field.endpoint || `/api/upload/${context.contextId}/${field.name}`;
                            const resp = await fetch(endpoint, {
                                method: 'POST',
                                body: formData
                            });
                            const result = await resp.json();
                            if (status) {
                                if (result.success) {
                                    status.textContent = result.message || 'Upload successful.';
                                    status.className = 'file-status success';
                                    input.value = ''; // Clear selection
                                    if (filenameEl) {
                                        filenameEl.textContent = 'No file selected';
                                        filenameEl.classList.remove('has-file');
                                    }
                                    if (btn) btn.disabled = true;
                                } else {
                                    status.textContent = 'Error: ' + (result.error || 'Upload failed');
                                    status.className = 'file-status error';
                                }
                            }
                        } catch (err) {
                            if (status) {
                                status.textContent = 'Error: ' + err.message;
                                status.className = 'file-status error';
                            }
                        } finally {
                            if (btn) btn.disabled = !input.files.length; // keep disabled if no file, though we cleared it on success
                            input.disabled = false;
                            if (selectBtn) selectBtn.disabled = false;
                        }
                    });
                }
                return; // Skip standard change listener
            }

            const el = card.querySelector(`#${fieldId}`);
            if (!el) return;

            const eventType = (field.type === this.WebUIFieldType.Slider) ? 'input' : (field.type === this.WebUIFieldType.Button ? 'click' : 'change');

            el.addEventListener(eventType, async (e) => {
                // Ignore programmatically dispatched events to prevent feedback loops
                if (!e.isTrusted) return;
                let value;
                if (field.type === this.WebUIFieldType.Button) {
                    value = 'clicked';
                } else {
                    value = e.target.type === 'checkbox' ? e.target.checked :
                        e.target.multiple ? Array.from(e.target.selectedOptions, option => option.value) : e.target.value;
                }
                if (card.dataset.editing === 'true' && field.type !== this.WebUIFieldType.Button) {
                    this.bufferFieldChange(card, field.name, value);
                    return;
                }
                // Pause polling to free heap for POST on ESP8266
                this.stopPolling();
                await new Promise(r => setTimeout(r, 600));
                await this.sendUICommand(context.contextId, field.name, value);
                // Apply theme and primary color immediately on client side when changed from settings
                if (context.contextId === 'webui_settings') {
                    if (field.name === 'theme') {
                        this.applyTheme(value);
                    } else if (field.name === 'primary_color') {
                        this.applyPrimaryColor(value);
                    }
                }
                await new Promise(r => setTimeout(r, 400));
                this.startPolling();
            });
        });
    }

    attachCardActions(card, context) {
        if (context.location !== this.WebUILocation.Settings || context.alwaysInteractive) return;
        const editBtn = card.querySelector('.btn-edit');
        const saveBtn = card.querySelector('.btn-save');
        const cancelBtn = card.querySelector('.btn-cancel');
        if (!editBtn || !saveBtn || !cancelBtn) return;

        const enterEdit = () => {
            if (card.dataset.editing === 'true') return;
            card.dataset.editing = 'true';
            this.editingContexts.add(context.contextId);
            const baseline = {};
            context.fields.forEach(f => {
                const el = card.querySelector(`#${context.contextId}_${f.name}`);
                if (!el) return;
                baseline[f.name] = f.type === this.WebUIFieldType.OrderedList
                    ? this.getOrderedListValue(el)
                    : (el.type === 'checkbox') ? el.checked : el.value;
            });
            card.dataset.baseline = JSON.stringify(baseline);
            card.dataset.pending = '{}';
            editBtn.style.display = 'none';
            saveBtn.style.display = '';
            cancelBtn.style.display = '';
            this.setCardEditing(card, true);
        };

        const applySave = async () => {
            const pending = card.dataset.pending ? JSON.parse(card.dataset.pending) : {};
            const entries = Object.entries(pending);
            // Sort: send boolean/toggle fields LAST so text fields (ssid, password)
            // are applied before the toggle triggers server-side actions (e.g. wifi_enabled)
            const fieldTypes = {};
            if (Array.isArray(context.fields)) {
                context.fields.forEach(f => { fieldTypes[f.name] = f.type; });
            }
            entries.sort((a, b) => {
                const aIsBool = (fieldTypes[a[0]] === this.WebUIFieldType.Boolean) ? 1 : 0;
                const bIsBool = (fieldTypes[b[0]] === this.WebUIFieldType.Boolean) ? 1 : 0;
                return aIsBool - bIsBool;
            });
            // Stop polling to free TCP/heap for requests (ESP8266 has ~3KB free)
            this.stopPolling();
            await new Promise(r => setTimeout(r, 600));
            for (const [fieldName, value] of entries) {
                await this.sendUICommand(context.contextId, fieldName, value);
                // Delay between requests to let ESP8266 reclaim TCP memory
                await new Promise(r => setTimeout(r, 400));
                if (context.contextId === 'webui_settings') {
                    if (fieldName === 'theme') {
                        this.applyTheme(value);
                    } else if (fieldName === 'primary_color') {
                        this.applyPrimaryColor(value);
                    }
                }
            }
            card.dataset.pending = '{}';
            exitEdit();
            // Resume polling after save completes
            this.startPolling();
        };

        const exitEdit = () => {
            card.dataset.editing = 'false';
            this.editingContexts.delete(context.contextId);
            editBtn.style.display = '';
            saveBtn.style.display = 'none';
            cancelBtn.style.display = 'none';
            this.setCardEditing(card, false);
        };

        const cancelEdit = () => {
            const baseline = card.dataset.baseline ? JSON.parse(card.dataset.baseline) : {};
            Object.entries(baseline).forEach(([name, value]) => {
                const field = context.fields.find(item => item.name === name);
                const el = card.querySelector(`#${context.contextId}_${name}`);
                if (!el) return;
                if (field && field.type === this.WebUIFieldType.OrderedList) {
                    this.renderOrderedListItems(el, value, field.optionLabels);
                } else if (el.type === 'checkbox') {
                    el.checked = (value === true || value === 'true');
                } else {
                    el.value = value;
                }
            });
            card.dataset.pending = '{}';
            exitEdit();
        };

        editBtn.addEventListener('click', enterEdit);
        saveBtn.addEventListener('click', applySave);
        cancelBtn.addEventListener('click', cancelEdit);
    }

    bufferFieldChange(card, fieldName, value) {
        const pending = card.dataset.pending ? JSON.parse(card.dataset.pending) : {};
        pending[fieldName] = value;
        card.dataset.pending = JSON.stringify(pending);
    }

    setCardEditing(card, isEditing) {
        const controls = card.querySelectorAll('.card-content input, .card-content select, .card-content textarea, .card-content button');
        controls.forEach(el => {
            if (el.dataset.wasDisabled === undefined) {
                el.dataset.wasDisabled = el.disabled ? 'true' : 'false';
            }
            if (el.id === 'otaAutoReboot') {
                el.disabled = true;
                return;
            }
            if (isEditing) {
                if (el.dataset.wasDisabled === 'false') {
                    el.disabled = false;
                }
            } else {
                el.disabled = true;
            }
        });
        card.querySelectorAll('.ordered-list-item').forEach(item => {
            item.draggable = isEditing;
        });
        card.querySelectorAll('.ordered-list').forEach(list => {
            list.classList.toggle('editing', isEditing);
        });
    }

    makeDeviceNameEditable(element) {
        if (element.querySelector('input')) return; // Already editing

        const currentName = element.textContent;
        element.innerHTML = ''; // Clear the h1

        const input = document.createElement('input');
        input.type = 'text';
        input.value = currentName;
        input.className = 'device-name-input';
        this.isEditingDeviceName = true;
        
        const saveName = () => {
            const newName = input.value.trim();
            if (newName && newName !== currentName) {
                element.textContent = newName;
                // Device name is now managed by System Info
                this.sendUICommand('system_info', 'device_name', newName);
            } else {
                element.textContent = currentName; // Revert if empty or unchanged
            }
            this.isEditingDeviceName = false;
        };

        input.addEventListener('blur', saveName);
        input.addEventListener('keydown', e => {
            if (e.key === 'Enter') {
                input.blur();
            } else if (e.key === 'Escape') {
                element.textContent = currentName;
                input.removeEventListener('blur', saveName); // prevent saving on blur
                this.isEditingDeviceName = false;
            }
        });

        element.appendChild(input);
        input.focus();
        input.select();
    }

    updateCharts(contexts) {
        // Generic chart update logic - works with any chart context
        Object.entries(contexts).forEach(([contextId, contextData]) => {
            // Look for chart data fields (ending with '_data')
            Object.entries(contextData).forEach(([fieldName, fieldValue]) => {
                if (fieldName.endsWith('_data')) {
                    try {
                        const chartData = JSON.parse(fieldValue);
                        if (!window.systemChartData) window.systemChartData = {};
                        
                        // Store chart data using the base field name (remove '_data' suffix)
                        const chartName = fieldName.replace('_data', '');
                        window.systemChartData[chartName] = chartData;
                        
                        // Try to call the chart-specific update function
                        const updateFunctionName = `update${contextId}Chart`;
                        if (typeof window[updateFunctionName] === 'function') {
                            window[updateFunctionName]();
                        }
                    } catch (e) {
                        console.warn(`Failed to parse chart data for ${contextId}.${fieldName}:`, e);
                    }
                }
            });
        });
    }

    async sendUICommand(contextId, fieldName, value) {
        const params = new URLSearchParams({
            contextId: contextId,
            field: fieldName,
            value: Array.isArray(value) ? JSON.stringify(value) : String(value)
        });
        try {
            const resp = await fetch('/api/ui/action?' + params.toString());
            if (resp.ok) {
                const data = await resp.json();
                if (data.error) {
                    console.warn('UI command error:', data.error);
                    alert(data.error);
                }
                return data;
            }
        } catch(err) {
            console.error('UI command failed:', err);
        }
        return null;
    }

    updateChart(fieldId, value, unit) {
        // Store value in history
        if (!this.chartData.has(fieldId)) {
            this.chartData.set(fieldId, []);
        }
        const history = this.chartData.get(fieldId);
        history.push(value);
        if (history.length > this.maxChartPoints) {
            history.shift();
        }

        // Update display value
        const valueEl = document.getElementById(`${fieldId}_value`);
        if (valueEl) {
            valueEl.textContent = `${value.toFixed(1)}${unit}`;
        }

        // Draw chart
        this.drawChart(fieldId, history, unit);
    }

    drawChart(fieldId, data, unit) {
        const canvas = document.getElementById(`${fieldId}_canvas`);
        if (!canvas) return;

        const ctx = canvas.getContext('2d');
        const width = canvas.width;
        const height = canvas.height;

        // Clear canvas
        ctx.clearRect(0, 0, width, height);

        if (!data || data.length === 0) return;

        // Draw grid lines
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }

        // Determine scale (0-100 for percentages, auto for others)
        const maxValue = unit === '%' ? 100 : Math.max(...data, 1);
        const minValue = 0;

        // Draw line
        ctx.strokeStyle = '#007acc';
        ctx.lineWidth = 2;
        ctx.beginPath();

        const stepX = width / Math.max(data.length - 1, 1);
        data.forEach((val, i) => {
            const x = i * stepX;
            const y = height - ((val - minValue) / (maxValue - minValue)) * height * 0.9;
            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        });
        ctx.stroke();

        // Fill area under curve
        ctx.fillStyle = 'rgba(0, 122, 204, 0.2)';
        ctx.lineTo(width, height);
        ctx.lineTo(0, height);
        ctx.closePath();
        ctx.fill();
    }

    injectCustomStyles() {
        // Remove any existing custom styles
        const existingStyle = document.getElementById('component-custom-styles');
        if (existingStyle) {
            existingStyle.remove();
        }

        // Collect all custom CSS from components
        let combinedCss = '';
        this.uiSchema.forEach(context => {
            if (context.customCss) {
                combinedCss += `/* ${context.contextId} */\n${context.customCss}\n\n`;
            }
        });

        if (combinedCss) {
            const styleElement = document.createElement('style');
            styleElement.id = 'component-custom-styles';
            styleElement.textContent = combinedCss;
            document.head.appendChild(styleElement);
        }
    }

    injectCustomScripts() {
        // Remove any existing custom scripts
        const existingScript = document.getElementById('component-custom-scripts');
        if (existingScript) {
            existingScript.remove();
        }

        // Collect all custom JS from components
        let combinedJs = '';
        this.uiSchema.forEach(context => {
            if (context.customJs) {
                combinedJs += `/* ${context.contextId} */\n${context.customJs}\n\n`;
            }
        });

        if (combinedJs) {
            const scriptElement = document.createElement('script');
            scriptElement.id = 'component-custom-scripts';
            scriptElement.textContent = combinedJs;
            document.head.appendChild(scriptElement);
        }
    }
}

document.addEventListener('DOMContentLoaded', () => {
  new DomoticsApp();
});
