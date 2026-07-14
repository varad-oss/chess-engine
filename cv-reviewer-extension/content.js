/**
 * CV Reviewer Content Script (content.js)
 * Injects a floating action button (FAB) that opens a Grammarly-style review sidebar.
 * Scans page textareas, renders them in a CV preview document, highlights issues,
 * and allows clicking highlights to accept suggestions and sync changes back.
 */

// Keep track of detected textareas and their current state
const pageTextareas = new Map();
let sidebarOpen = false;
let activeHighlightElement = null;

// Weak verbs to flag and suggest alternatives
const WEAK_VERBS_MAP = {
  'helped': ['spearheaded', 'facilitated', 'collaborated on', 'empowered'],
  'worked on': ['architected', 'engineered', 'developed', 'optimized'],
  'responsible for': ['pioneered', 'spearheaded', 'drove', 'executed'],
  'assisted': ['collaborated on', 'supported', 'facilitated'],
  'managed': ['orchestrated', 'spearheaded', 'directed', 'steered'],
  'led': ['spearheaded', 'pioneered', 'guided', 'piloted'],
  'handled': ['executed', 'resolved', 'managed', 'administered'],
  'utilized': ['leveraged', 'deployed', 'implemented'],
  'did': ['executed', 'conducted', 'dispatched'],
  'created': ['formulated', 'devised', 'pioneered', 'authored'],
  'made': ['constructed', 'forged', 'generated'],
  'built': ['engineered', 'architected', 'implemented']
};

// Core technologies for bolding suggestions
const CORE_TECH_KEYWORDS = [
  'C\\+\\+', 'Python', 'React', 'Docker', 'AWS', 'SQL', 'TypeScript', 'Java', 'Kubernetes',
  'Node\\.js', 'Go', 'Rust', 'Kafka', 'Redis', 'GraphQL', 'GCP', 'JavaScript', 'HTML', 'CSS',
  'TensorFlow', 'PyTorch', 'PostgreSQL', 'MongoDB', 'Git', 'Jenkins'
];

// Injected sidebar host and references
let sidebarHost = null;
let sidebarShadow = null;
let fabBtn = null;

/**
 * Initialize extension on page
 */
function init() {
  createFAB();
  createSidebar();
  scanPageTextareas();

  // Watch for page structural changes to capture dynamic fields (SPAs)
  const observer = new MutationObserver(() => {
    scanPageTextareas();
    if (sidebarOpen) {
      renderPreviewList();
    }
  });
  observer.observe(document.body, { childList: true, subtree: true });
}

/**
 * Creates the floating action button (FAB) on the host page.
 */
function createFAB() {
  if (document.getElementById('cv-reviewer-fab-root')) return;

  const root = document.createElement('div');
  root.id = 'cv-reviewer-fab-root';
  root.style.position = 'fixed';
  root.style.bottom = '24px';
  root.style.right = '24px';
  root.style.zIndex = '999999';
  document.body.appendChild(root);

  const shadow = root.attachShadow({ mode: 'open' });

  // FAB Styles
  const style = document.createElement('style');
  style.textContent = `
    .fab {
      background: linear-gradient(135deg, #6366f1 0%, #4f46e5 100%);
      color: white;
      border: none;
      border-radius: 50px;
      padding: 12px 20px;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      font-size: 13.5px;
      font-weight: 700;
      cursor: pointer;
      box-shadow: 0 8px 24px rgba(79, 70, 229, 0.4);
      display: flex;
      align-items: center;
      gap: 8px;
      transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    }
    .fab:hover {
      transform: translateY(-2px);
      box-shadow: 0 12px 30px rgba(79, 70, 229, 0.6);
    }
    .fab:active {
      transform: translateY(1px);
    }
    .badge {
      background: #10b981;
      color: white;
      font-size: 10px;
      font-weight: 800;
      padding: 1px 6px;
      border-radius: 10px;
      margin-left: 2px;
    }
  `;
  shadow.appendChild(style);

  fabBtn = document.createElement('button');
  fabBtn.className = 'fab';
  fabBtn.innerHTML = `
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
      <polyline points="14 2 14 8 20 8"></polyline>
      <line x1="16" y1="13" x2="8" y2="13"></line>
      <line x1="16" y1="17" x2="8" y2="17"></line>
      <polyline points="10 9 9 9 8 9"></polyline>
    </svg>
    Review CV <span class="badge" id="fab-count">0</span>
  `;
  
  fabBtn.addEventListener('click', toggleSidebar);
  shadow.appendChild(fabBtn);
}

/**
 * Creates the review sidebar structure and appends it to the DOM.
 */
function createSidebar() {
  if (document.getElementById('cv-reviewer-sidebar-root')) return;

  sidebarHost = document.createElement('div');
  sidebarHost.id = 'cv-reviewer-sidebar-root';
  sidebarHost.style.position = 'fixed';
  sidebarHost.style.top = '0';
  sidebarHost.style.right = '0';
  sidebarHost.style.height = '100vh';
  sidebarHost.style.width = '0'; // Hidden by default
  sidebarHost.style.zIndex = '999999';
  sidebarHost.style.transition = 'width 0.3s cubic-bezier(0.16, 1, 0.3, 1)';
  document.body.appendChild(sidebarHost);

  sidebarShadow = sidebarHost.attachShadow({ mode: 'open' });

  // Sidebar CSS Stylesheet
  const style = document.createElement('style');
  style.textContent = `
    :host {
      box-sizing: border-box;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    }
    
    .sidebar-panel {
      position: absolute;
      top: 0;
      right: 0;
      width: 460px;
      height: 100vh;
      background: #0b0f19;
      border-left: 1px solid rgba(255, 255, 255, 0.08);
      box-shadow: -8px 0 32px rgba(0, 0, 0, 0.5);
      display: flex;
      flex-direction: column;
      color: #e2e8f0;
      overflow: hidden;
    }

    .header {
      padding: 16px 20px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.08);
      display: flex;
      justify-content: space-between;
      align-items: center;
      background: #0f172a;
    }

    .title-group {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .title-group h2 {
      font-size: 16px;
      font-weight: 700;
      margin: 0;
      background: linear-gradient(135deg, #a5b4fc 0%, #34d399 100%);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }

    .header-actions {
      display: flex;
      align-items: center;
      gap: 12px;
    }

    .header-btn {
      background: none;
      border: none;
      color: #94a3b8;
      cursor: pointer;
      font-size: 13px;
      font-weight: 600;
      display: flex;
      align-items: center;
      gap: 4px;
      transition: color 0.15s;
    }

    .header-btn:hover {
      color: #f1f5f9;
    }

    .close-btn {
      font-size: 20px;
      background: none;
      border: none;
      color: #94a3b8;
      cursor: pointer;
      line-height: 1;
      padding: 4px;
      transition: color 0.15s;
    }

    .close-btn:hover {
      color: #f1f5f9;
    }

    .content-area {
      flex: 1;
      overflow-y: auto;
      padding: 20px;
      display: flex;
      flex-direction: column;
      gap: 20px;
      scrollbar-width: thin;
      scrollbar-color: rgba(255, 255, 255, 0.1) transparent;
    }

    .empty-state {
      text-align: center;
      padding: 40px 20px;
      color: #64748b;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 12px;
    }

    .empty-state svg {
      color: #334155;
    }

    /* CV Document Card */
    .bullet-card {
      background: #111827;
      border: 1px solid rgba(255, 255, 255, 0.05);
      border-radius: 12px;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 12px;
      position: relative;
      transition: border-color 0.2s;
    }

    .bullet-card:hover {
      border-color: rgba(99, 102, 241, 0.2);
    }

    .card-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 11.5px;
      color: #64748b;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }

    .field-label {
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .score-badge {
      background: rgba(99, 102, 241, 0.15);
      color: #a5b4fc;
      border-radius: 4px;
      padding: 2px 6px;
      font-size: 10px;
    }

    /* Preview Editor Block */
    .preview-editor {
      background: #0b0f19;
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 8px;
      padding: 12px;
      font-size: 13.5px;
      line-height: 1.6;
      color: #e2e8f0;
      min-height: 50px;
      outline: none;
      word-break: break-word;
    }

    .preview-editor:focus {
      border-color: #4f46e5;
    }

    /* Grammarly-style Highlights */
    .cv-highlight {
      position: relative;
      cursor: pointer;
      transition: background 0.15s;
      border-bottom: 2px dotted transparent;
      padding: 1px 0;
    }

    .cv-highlight.weak-verb {
      border-bottom-color: #ef4444; /* Dotted Red Underline */
      background: rgba(239, 68, 68, 0.08);
    }
    
    .cv-highlight.weak-verb:hover {
      background: rgba(239, 68, 68, 0.16);
    }

    .cv-highlight.bold-tech {
      border-bottom-color: #10b981; /* Dotted Green Underline */
      background: rgba(16, 185, 129, 0.08);
    }
    
    .cv-highlight.bold-tech:hover {
      background: rgba(16, 185, 129, 0.16);
    }

    .cv-highlight.active {
      background: rgba(99, 102, 241, 0.25) !important;
      border-bottom-style: solid;
    }

    .error-tag {
      display: inline-flex;
      align-items: center;
      gap: 4px;
      font-size: 11px;
      font-weight: 700;
      border-radius: 4px;
      padding: 1px 6px;
      margin-left: 4px;
      vertical-align: middle;
      cursor: pointer;
    }

    .error-tag.missing-metric {
      background: rgba(245, 158, 11, 0.15);
      border: 1px solid rgba(245, 158, 11, 0.3);
      color: #fbbf24;
    }

    .error-tag.dangling-word {
      background: rgba(59, 130, 246, 0.15);
      border: 1px solid rgba(59, 130, 246, 0.3);
      color: #60a5fa;
    }

    /* Suggestion Popover Card */
    .suggestion-box {
      background: #1f2937;
      border: 1px solid rgba(255, 255, 255, 0.08);
      border-radius: 8px;
      padding: 12px;
      display: none;
      flex-direction: column;
      gap: 10px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.3);
      animation: fadeIn 0.2s ease-out;
    }

    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(2px); }
      to { opacity: 1; transform: translateY(0); }
    }

    .suggestion-title {
      font-size: 12px;
      font-weight: 700;
      color: #94a3b8;
      display: flex;
      align-items: center;
      gap: 6px;
    }

    .suggestion-desc {
      font-size: 12px;
      color: #cbd5e1;
      line-height: 1.4;
    }

    .option-list {
      display: flex;
      flex-wrap: wrap;
      gap: 6px;
    }

    .option-btn {
      background: #4f46e5;
      color: white;
      border: none;
      border-radius: 6px;
      padding: 5px 12px;
      font-size: 11.5px;
      font-weight: 600;
      cursor: pointer;
      transition: background 0.15s;
    }

    .option-btn:hover {
      background: #6366f1;
    }

    .option-btn-secondary {
      background: rgba(255, 255, 255, 0.06);
      color: #e2e8f0;
      border: 1px solid rgba(255, 255, 255, 0.08);
    }

    .option-btn-secondary:hover {
      background: rgba(255, 255, 255, 0.12);
    }

    /* Actions Drawer */
    .card-footer {
      display: flex;
      justify-content: flex-end;
      gap: 8px;
      border-top: 1px solid rgba(255, 255, 255, 0.04);
      padding-top: 12px;
    }

    .action-btn {
      background: rgba(255, 255, 255, 0.06);
      color: #cbd5e1;
      border: 1px solid rgba(255, 255, 255, 0.08);
      padding: 6px 12px;
      border-radius: 6px;
      font-size: 11.5px;
      font-weight: 600;
      cursor: pointer;
      display: flex;
      align-items: center;
      gap: 6px;
      transition: all 0.15s;
    }

    .action-btn:hover {
      background: rgba(255, 255, 255, 0.12);
      color: white;
    }

    .action-btn-primary {
      background: linear-gradient(135deg, #6366f1 0%, #4f46e5 100%);
      color: white;
      border: none;
      box-shadow: 0 4px 12px rgba(79, 70, 229, 0.25);
    }

    .action-btn-primary:hover {
      background: #6366f1;
      box-shadow: 0 6px 16px rgba(79, 70, 229, 0.4);
    }

    .spinner {
      animation: spin 1s linear infinite;
    }
    @keyframes spin { to { transform: rotate(360deg); } }
  `;

  sidebarShadow.appendChild(style);

  // Panel layout
  const panel = document.createElement('div');
  panel.className = 'sidebar-panel';
  panel.innerHTML = `
    <div class="header">
      <div class="title-group">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#818cf8" stroke-width="2.5">
          <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
          <polyline points="14 2 14 8 20 8"></polyline>
        </svg>
        <h2>CV Impact Reviewer</h2>
      </div>
      <div class="header-actions">
        <button class="header-btn" id="sidebar-refresh-btn">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
            <path d="M23 4v6h-6M1 20v-6h6M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"></path>
          </svg>
          Scan Page
        </button>
        <button class="close-btn" id="sidebar-close-btn">&times;</button>
      </div>
    </div>
    <div class="content-area" id="sidebar-content">
      <!-- Generated preview items render here -->
    </div>
  `;

  sidebarShadow.appendChild(panel);

  // Setup click bindings
  sidebarShadow.getElementById('sidebar-close-btn').addEventListener('click', toggleSidebar);
  sidebarShadow.getElementById('sidebar-refresh-btn').addEventListener('click', () => {
    scanPageTextareas();
    renderPreviewList();
  });
}

/**
 * Open/Close the slide-out Shadow DOM sidebar.
 */
function toggleSidebar() {
  sidebarOpen = !sidebarOpen;
  if (sidebarOpen) {
    scanPageTextareas();
    renderPreviewList();
    sidebarHost.style.width = '460px';
  } else {
    sidebarHost.style.width = '0';
    activeHighlightElement = null;
  }
}

/**
 * Scans the active page for textareas, maps them, and listens for live typing syncs.
 */
function scanPageTextareas() {
  const textareas = document.querySelectorAll('textarea');
  
  // Clean up any textareas that were removed from the DOM
  for (const [textarea, id] of pageTextareas.entries()) {
    if (!document.body.contains(textarea)) {
      pageTextareas.delete(textarea);
    }
  }

  let index = 0;
  textareas.forEach(textarea => {
    // Filter out very small elements
    if (textarea.offsetHeight < 30 || textarea.offsetWidth < 80) return;

    if (!pageTextareas.has(textarea)) {
      const id = `cv-txt-${index++}`;
      textarea.setAttribute('data-cv-textarea-id', id);
      pageTextareas.set(textarea, id);

      // Listen for changes on the host page textarea to sync it live to the sidebar preview
      textarea.addEventListener('input', () => {
        if (sidebarOpen) {
          const previewBlock = sidebarShadow.getElementById(`preview-${id}`);
          if (previewBlock && document.activeElement !== textarea) {
            // Re-render only if the user is not actively editing in the preview block
            updatePreviewBlockContent(id, textarea.value);
          }
        }
      });
    }
  });

  // Update FAB badge counter
  if (fabBtn) {
    const badge = fabBtn.querySelector('#fab-count');
    if (badge) {
      badge.textContent = pageTextareas.size;
    }
  }
}

/**
 * Renders the preview cards for all detected textareas inside the sidebar.
 */
function renderPreviewList() {
  const container = sidebarShadow.getElementById('sidebar-content');
  container.innerHTML = ''; // Clear previous items

  if (pageTextareas.size === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <rect x="2" y="2" width="20" height="20" rx="2" ry="2"></rect>
          <path d="M12 18h.01M8 6h8M8 10h8M8 14h4"></path>
        </svg>
        <h3>No Bullet Fields Found</h3>
        <p style="font-size: 12.5px; margin: 0; line-height: 1.5;">We couldn't detect any active resume input textareas on the webpage. Open a university portal form or text editor to begin.</p>
      </div>
    `;
    return;
  }

  pageTextareas.forEach((id, textarea) => {
    const card = document.createElement('div');
    card.className = 'bullet-card';
    card.id = `card-${id}`;
    
    card.innerHTML = `
      <div class="card-header">
        <span class="field-label">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
            <line x1="8" y1="6" x2="21" y2="6"></line>
            <line x1="8" y1="12" x2="21" y2="12"></line>
            <line x1="8" y1="18" x2="21" y2="18"></line>
            <line x1="3" y1="6" x2="3.01" y2="6"></line>
            <line x1="3" y1="12" x2="3.01" y2="12"></line>
            <line x1="3" y1="18" x2="3.01" y2="18"></line>
          </svg>
          Bullet Field #${id.split('-').pop()}
        </span>
        <span class="score-badge" id="score-badge-${id}">Score: 0/100</span>
      </div>

      <!-- Preview container displaying Grammarly highlights -->
      <div class="preview-editor" id="preview-${id}" contenteditable="true" placeholder="Enter bullet point..."></div>

      <!-- Micro Suggestion Popover -->
      <div class="suggestion-box" id="suggestion-box-${id}"></div>

      <!-- Action Footer -->
      <div class="card-footer">
        <button class="action-btn action-btn-primary" id="btn-rewrite-${id}">
          <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
            <path d="M12 2v20M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6"/>
          </svg>
          FAANG LLM Rewrite
        </button>
      </div>
    `;

    container.appendChild(card);

    // Initial load of content and parsing
    const text = textarea.value;
    updatePreviewBlockContent(id, text);

    // Set up card event handling
    setupCardActions(id, card, textarea);
  });
}

/**
 * Formats a bullet point's text with Grammarly-style spans and sets innerHTML.
 */
function updatePreviewBlockContent(id, text) {
  const previewBlock = sidebarShadow.getElementById(`preview-${id}`);
  if (!previewBlock) return;

  const textVal = text.trim();
  if (!textVal) {
    previewBlock.innerHTML = '';
    updateScoreBadge(id, 0);
    return;
  }

  // Perform checks and construct highlights
  const highlightedHtml = generateHighlightedText(textVal);
  previewBlock.innerHTML = highlightedHtml;

  // Recalculate score badge based on metrics
  calculateAndRenderScore(id, textVal);
}

/**
 * Run rules checks and inject HTML spans for Grammarly marks.
 */
function generateHighlightedText(text) {
  let processedText = text;

  // 1. Tag weak verbs using boundary matches
  for (const [weakVerb, suggestions] of Object.entries(WEAK_VERBS_MAP)) {
    const regex = new RegExp(`\\b(${weakVerb})\\b`, 'gi');
    processedText = processedText.replace(regex, (match) => {
      return `<span class="cv-highlight weak-verb" data-type="verb" data-word="${match}" data-suggestions="${suggestions.join(',')}">${match}</span>`;
    });
  }

  // 2. Tag technologies for bolding suggestions
  CORE_TECH_KEYWORDS.forEach(techPattern => {
    // Match only if it is not inside an HTML tag attribute or wrapped in <b>/<strong> tags
    // A simplified regex boundary search
    const regex = new RegExp(`\\b(${techPattern})\\b(?!([^<]*>)|([^<]*<\/b>))`, 'gi');
    processedText = processedText.replace(regex, (match) => {
      // Avoid tagging it if it is already inside a span highlight
      return `<span class="cv-highlight bold-tech" data-type="bold" data-word="${match}">${match}</span>`;
    });
  });

  // 3. Append warning tags for missing metrics (Quantification)
  const hasNumbers = /\b\d+%?\b|\$\b\d+\b|\b\d+\s*(k|m|b|million|billion|thousand|percent)\b/i.test(text);
  if (!hasNumbers) {
    processedText += ` <span class="error-tag missing-metric" data-type="quant" title="Click to fix quantification issue">⚠️ Metric Missing</span>`;
  }

  // 4. Append warning tag for Dangling Line wrap (Whitespace)
  const lineCharLimit = 85;
  const remainder = text.length % lineCharLimit;
  if (text.length > lineCharLimit && remainder > 0 && remainder <= 12) {
    processedText += ` <span class="error-tag dangling-word" data-type="dangling" data-remainder="${remainder}" title="Click to inspect dangling line issue">📏 Dangling Wrap (${remainder} chars)</span>`;
  }

  return processedText;
}

/**
 * Calculates a dynamic score (0-100) and displays it.
 */
function calculateAndRenderScore(id, text) {
  let score = 80; // Baseline score
  
  // Check weak verbs count
  let weakVerbCount = 0;
  for (const weakVerb of Object.keys(WEAK_VERBS_MAP)) {
    const regex = new RegExp(`\\b${weakVerb}\\b`, 'i');
    if (regex.test(text)) weakVerbCount++;
  }
  score -= weakVerbCount * 12;

  // Check metrics
  const hasNumbers = /\b\d+%?\b|\$\b\d+\b|\b\d+\s*(k|m|b|million|billion|thousand|percent)\b/i.test(text);
  if (!hasNumbers) score -= 25;

  // Check tech keywords presence
  let techCount = 0;
  CORE_TECH_KEYWORDS.forEach(techPattern => {
    const regex = new RegExp(`\\b${techPattern}\\b`, 'i');
    if (regex.test(text)) techCount++;
  });
  if (techCount > 0) score += Math.min(15, techCount * 5); // Tech boosts score

  // Check dangling words
  const remainder = text.length % 85;
  if (text.length > 85 && remainder > 0 && remainder <= 12) score -= 8;

  const finalScore = Math.max(10, Math.min(100, score));
  updateScoreBadge(id, finalScore);
}

function updateScoreBadge(id, score) {
  const badge = sidebarShadow.getElementById(`score-badge-${id}`);
  if (badge) {
    badge.textContent = `Score: ${score}/100`;
    if (score >= 90) {
      badge.style.background = 'rgba(16, 185, 129, 0.15)';
      badge.style.color = '#34d399';
    } else if (score >= 70) {
      badge.style.background = 'rgba(245, 158, 11, 0.15)';
      badge.style.color = '#fbbf24';
    } else {
      badge.style.background = 'rgba(239, 68, 68, 0.15)';
      badge.style.color = '#f87171';
    }
  }
}

/**
 * Binds events to the preview editor, suggestion popups, and footer button.
 */
function setupCardActions(id, card, textarea) {
  const previewBlock = sidebarShadow.getElementById(`preview-${id}`);
  const suggestionBox = sidebarShadow.getElementById(`suggestion-box-${id}`);
  const rewriteBtn = sidebarShadow.getElementById(`btn-rewrite-${id}`);

  // 1. Sync manual typing in preview back to original textarea
  previewBlock.addEventListener('input', () => {
    // Extract plain text value (remove HTML highlight elements)
    const text = previewBlock.innerText.replace(/⚠️ Metric Missing/g, '').replace(/📏 Dangling Wrap \(\d+ chars\)/g, '').trim();
    textarea.value = text;
    
    // Trigger input/change updates on the host page
    textarea.dispatchEvent(new Event('input', { bubbles: true }));
    textarea.dispatchEvent(new Event('change', { bubbles: true }));

    // Recompute score on active typing
    calculateAndRenderScore(id, text);
  });

  // Re-run highlighting on focus blur (avoiding caret moves on typing)
  previewBlock.addEventListener('blur', () => {
    const text = textarea.value;
    updatePreviewBlockContent(id, text);
  });

  // 2. Grammarly highlight click event (Opens Suggestion Box)
  previewBlock.addEventListener('click', (e) => {
    const target = e.target;
    
    if (target.classList.contains('cv-highlight') || target.classList.contains('error-tag')) {
      e.stopPropagation();
      
      // Deactivate previous active highlight
      if (activeHighlightElement) {
        activeHighlightElement.classList.remove('active');
      }
      
      activeHighlightElement = target;
      target.classList.add('active');

      showSuggestionBox(id, target, suggestionBox, textarea, previewBlock);
    } else {
      // Clicked outside highlight - close suggestion box
      suggestionBox.style.display = 'none';
      if (activeHighlightElement) {
        activeHighlightElement.classList.remove('active');
        activeHighlightElement = null;
      }
    }
  });

  // 3. FAANG Rewrite (LLM Integration)
  rewriteBtn.addEventListener('click', (e) => {
    e.stopPropagation();
    const text = textarea.value.trim();
    if (!text) return;

    rewriteBtn.disabled = true;
    rewriteBtn.innerHTML = `
      <svg class="spinner" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" style="margin-right: 6px;">
        <circle cx="12" cy="12" r="10" stroke="rgba(255, 255, 255, 0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="white"></path>
      </svg>
      Generating...
    `;

    chrome.runtime.sendMessage({
      action: 'REWRITE_BULLET',
      bulletPoint: text
    }, (response) => {
      rewriteBtn.disabled = false;
      rewriteBtn.innerHTML = `
        <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
          <path d="M12 2v20M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6"/>
        </svg>
        FAANG LLM Rewrite
      `;

      if (chrome.runtime.lastError || !response) {
        alert('Failed to contact LLM backend. Please check the extension popup dashboard configurations.');
        return;
      }

      if (response.error) {
        alert(`API Error: ${response.error}`);
        return;
      }

      if (response.result) {
        // Apply rewrite back to preview and original inputs
        textarea.value = response.result;
        textarea.dispatchEvent(new Event('input', { bubbles: true }));
        textarea.dispatchEvent(new Event('change', { bubbles: true }));

        updatePreviewBlockContent(id, response.result);
        suggestionBox.style.display = 'none';

        // Increment total checked stats
        chrome.storage.local.get({ statsTotalChecked: 0 }, (data) => {
          chrome.storage.local.set({ statsTotalChecked: data.statsTotalChecked + 1 });
        });
      }
    });
  });
}

/**
 * Populates and reveals the suggestion box for a specific highlight click.
 */
function showSuggestionBox(id, element, box, textarea, previewBlock) {
  const type = element.getAttribute('data-type');
  const word = element.getAttribute('data-word');
  
  box.innerHTML = ''; // Reset suggestion card
  box.style.display = 'flex';

  if (type === 'verb') {
    const suggestions = element.getAttribute('data-suggestions').split(',');
    box.innerHTML = `
      <div class="suggestion-title">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="#ef4444" stroke-width="2.5">
          <circle cx="12" cy="12" r="10"></circle>
          <line x1="12" y1="8" x2="12" y2="12"></line>
          <line x1="12" y1="16" x2="12.01" y2="16"></line>
        </svg>
        Weak Action Verb
      </div>
      <div class="suggestion-desc">"${word}" is passive or weak. Replace it with a strong alternative:</div>
      <div class="option-list">
        ${suggestions.map(s => `<button class="option-btn" data-val="${s}">${s}</button>`).join('')}
        <button class="option-btn option-btn-secondary" id="dismiss-suggest-${id}">Dismiss</button>
      </div>
    `;

    // Bind suggestion clicks
    box.querySelectorAll('.option-btn').forEach(btn => {
      if (btn.id === `dismiss-suggest-${id}`) return;
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        const replacement = btn.getAttribute('data-val');
        
        // Match word boundary case insensitively and replace
        const regex = new RegExp(`\\b${word}\\b`, 'i');
        textarea.value = textarea.value.replace(regex, (match) => {
          if (match[0] === match[0].toUpperCase()) {
            return replacement.charAt(0).toUpperCase() + replacement.slice(1);
          }
          return replacement;
        });

        // Trigger update
        textarea.dispatchEvent(new Event('input', { bubbles: true }));
        textarea.dispatchEvent(new Event('change', { bubbles: true }));

        updatePreviewBlockContent(id, textarea.value);
        box.style.display = 'none';
        
        // Log increment stats in extension popup counter
        chrome.storage.local.get({ statsVerbsUpgraded: 0 }, (data) => {
          chrome.storage.local.set({ statsVerbsUpgraded: data.statsVerbsUpgraded + 1 });
        });
      });
    });

  } else if (type === 'bold') {
    box.innerHTML = `
      <div class="suggestion-title">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2.5">
          <circle cx="12" cy="12" r="10"></circle>
          <polyline points="12 16 12 12 12 8"></polyline>
        </svg>
        Formatting Suggestion
      </div>
      <div class="suggestion-desc">Recruiters scan quickly. Suggest wrapping technology <strong>${word}</strong> in bold tags.</div>
      <div class="option-list">
        <button class="option-btn" id="accept-bold-${id}">Bold Word</button>
        <button class="option-btn option-btn-secondary" id="dismiss-suggest-${id}">Dismiss</button>
      </div>
    `;

    box.querySelector(`#accept-bold-${id}`).addEventListener('click', (e) => {
      e.stopPropagation();
      const regex = new RegExp(`\\b${word}\\b`, 'i');
      textarea.value = textarea.value.replace(regex, `<b>${word}</b>`);

      textarea.dispatchEvent(new Event('input', { bubbles: true }));
      textarea.dispatchEvent(new Event('change', { bubbles: true }));

      updatePreviewBlockContent(id, textarea.value);
      box.style.display = 'none';
    });

  } else if (type === 'quant') {
    box.innerHTML = `
      <div class="suggestion-title">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="#fbbf24" stroke-width="2.5">
          <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
          <line x1="12" y1="9" x2="12" y2="13"></line>
          <line x1="12" y1="17" x2="12.01" y2="17"></line>
        </svg>
        Impact Metric Missing
      </div>
      <div class="suggestion-desc">Bullet is missing quantifiable metrics. Consider adding percentages, latency speedups, or scale values.</div>
      <div class="option-list">
        <button class="option-btn" id="insert-placeholder-${id}">Insert [by X%]</button>
        <button class="option-btn option-btn-secondary" id="dismiss-suggest-${id}">Dismiss</button>
      </div>
    `;

    box.querySelector(`#insert-placeholder-${id}`).addEventListener('click', (e) => {
      e.stopPropagation();
      // Insert placeholder before the final period or at the end
      if (textarea.value.endsWith('.')) {
        textarea.value = textarea.value.slice(0, -1) + ' resulting in a [X%] improvement.';
      } else {
        textarea.value += ' resulting in a [X%] improvement.';
      }

      textarea.dispatchEvent(new Event('input', { bubbles: true }));
      textarea.dispatchEvent(new Event('change', { bubbles: true }));

      updatePreviewBlockContent(id, textarea.value);
      box.style.display = 'none';
    });

  } else if (type === 'dangling') {
    const remainder = element.getAttribute('data-remainder');
    box.innerHTML = `
      <div class="suggestion-title">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="#3b82f6" stroke-width="2.5">
          <line x1="4" y1="9" x2="20" y2="9"></line>
          <line x1="4" y1="15" x2="20" y2="15"></line>
          <line x1="10" y1="3" x2="8" y2="21"></line>
          <line x1="16" y1="3" x2="14" y2="21"></line>
        </svg>
        Dangling Word Warning
      </div>
      <div class="suggestion-desc">Estimated line length suggests 1-2 final words wrapping onto a new line, wasting resume page space. Consider trimming ${remainder} characters or expanding.</div>
      <div class="option-list">
        <button class="option-btn option-btn-secondary" id="dismiss-suggest-${id}">Dismiss</button>
      </div>
    `;
  }

  // Dismiss button action for all types
  const dismissBtn = box.querySelector(`#dismiss-suggest-${id}`);
  if (dismissBtn) {
    dismissBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      box.style.display = 'none';
      if (activeHighlightElement) {
        activeHighlightElement.classList.remove('active');
        activeHighlightElement = null;
      }
    });
  }
}

// Global click listener to dismiss suggestion boxes on outside clicks
document.addEventListener('click', () => {
  if (sidebarOpen && sidebarShadow) {
    sidebarShadow.querySelectorAll('.suggestion-box').forEach(box => {
      box.style.display = 'none';
    });
    sidebarShadow.querySelectorAll('.cv-highlight').forEach(el => {
      el.classList.remove('active');
    });
    activeHighlightElement = null;
  }
});

// Run scan once page triggers idle state
init();
