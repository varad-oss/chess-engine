/**
 * CV Reviewer Action Popup (popup.js)
 * Manages configuration settings, the test playground, and statistics.
 */

// Model options mapping
const MODEL_OPTIONS = {
  gemini: [
    { value: 'gemini-2.5-flash', label: 'Gemini 2.5 Flash (Fast, Recommended)' },
    { value: 'gemini-2.5-pro', label: 'Gemini 2.5 Pro (More capable, Slower)' }
  ],
  openai: [
    { value: 'gpt-4o-mini', label: 'GPT-4o Mini (Cost-efficient, Fast)' },
    { value: 'gpt-4o', label: 'GPT-4o (High capability)' }
  ]
};

// Weak verbs list to count upgrades in stats
const WEAK_VERBS_LIST = [
  'helped', 'worked on', 'responsible for', 'assisted', 'managed', 'led', 'handled', 'utilized', 'did', 'created', 'made', 'built'
];

document.addEventListener('DOMContentLoaded', async () => {
  // UI Elements
  const tabButtons = document.querySelectorAll('.nav-btn');
  const tabPanes = document.querySelectorAll('.tab-pane');
  
  const providerSelect = document.getElementById('provider-select');
  const modelSelect = document.getElementById('model-select');
  const apiKeyInput = document.getElementById('api-key-input');
  const toggleKeyVisibilityBtn = document.getElementById('toggle-key-visibility');
  const saveSettingsBtn = document.getElementById('save-settings-btn');
  const saveStatus = document.getElementById('save-status');

  const sandboxTextarea = document.getElementById('sandbox-textarea');
  const sandboxOptimizeBtn = document.getElementById('sandbox-optimize-btn');
  const sandboxResult = document.getElementById('sandbox-result');
  const sandboxCopyBtn = document.getElementById('sandbox-copy-btn');

  const statsTotalChecked = document.getElementById('stats-total-checked');
  const statsVerbsUpgraded = document.getElementById('stats-verbs-upgraded');
  const statsScorePercent = document.getElementById('stats-score-percent');
  const statsProgressFill = document.getElementById('stats-progress-fill');

  // --- 1. Tab Switching Logic ---
  tabButtons.forEach(btn => {
    btn.addEventListener('click', () => {
      const targetTab = btn.getAttribute('data-tab');
      
      tabButtons.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');

      tabPanes.forEach(pane => {
        pane.classList.remove('active');
        if (pane.id === `${targetTab}-tab`) {
          pane.classList.add('active');
        }
      });

      if (targetTab === 'stats') {
        loadAndDisplayStats();
      }
    });
  });

  // --- 2. Dynamic Provider/Model Population ---
  providerSelect.addEventListener('change', () => {
    populateModels(providerSelect.value);
  });

  function populateModels(provider, selectedModelValue = '') {
    modelSelect.innerHTML = '';
    const options = MODEL_OPTIONS[provider] || [];
    options.forEach(opt => {
      const optionEl = document.createElement('option');
      optionEl.value = opt.value;
      optionEl.textContent = opt.label;
      if (opt.value === selectedModelValue) {
        optionEl.selected = true;
      }
      modelSelect.appendChild(optionEl);
    });
  }

  // --- 3. Load Saved Settings ---
  const storedConfig = await chrome.storage.local.get({
    provider: 'gemini',
    apiKey: '',
    model: 'gemini-2.5-flash'
  });

  providerSelect.value = storedConfig.provider;
  populateModels(storedConfig.provider, storedConfig.model);
  apiKeyInput.value = storedConfig.apiKey;

  // --- 4. Password Visibility Toggle ---
  toggleKeyVisibilityBtn.addEventListener('click', () => {
    const isPassword = apiKeyInput.type === 'password';
    apiKeyInput.type = isPassword ? 'text' : 'password';
    
    // Toggle icon style or icon itself
    const svg = toggleKeyVisibilityBtn.querySelector('svg');
    if (isPassword) {
      // Eye Off Icon
      svg.innerHTML = `
        <path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"></path>
        <line x1="1" y1="1" x2="23" y2="23"></line>
      `;
    } else {
      // Eye Icon
      svg.innerHTML = `
        <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path>
        <circle cx="12" cy="12" r="3"></circle>
      `;
    }
  });

  // --- 5. Save Settings Logic ---
  saveSettingsBtn.addEventListener('click', async () => {
    const provider = providerSelect.value;
    const model = modelSelect.value;
    const apiKey = apiKeyInput.value.trim();

    await chrome.storage.local.set({
      provider,
      model,
      apiKey
    });

    saveStatus.classList.add('show');
    setTimeout(() => {
      saveStatus.classList.remove('show');
    }, 2000);
  });

  // --- 6. Sandbox Playground Logic ---
  sandboxOptimizeBtn.addEventListener('click', async () => {
    const text = sandboxTextarea.value.trim();
    if (!text) {
      sandboxResult.className = 'sandbox-result-box';
      sandboxResult.textContent = 'Please enter a bullet point draft first.';
      sandboxCopyBtn.style.display = 'none';
      return;
    }

    sandboxResult.className = 'sandbox-result-box loading';
    sandboxResult.innerHTML = `
      <svg class="spinner" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" style="margin-right: 8px;">
        <circle cx="12" cy="12" r="10" stroke="rgba(255, 255, 255, 0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="#818cf8"></path>
      </svg>
      Optimizing CV Bullet...
    `;
    sandboxCopyBtn.style.display = 'none';

    chrome.runtime.sendMessage({
      action: 'REWRITE_BULLET',
      bulletPoint: text
    }, async (response) => {
      if (chrome.runtime.lastError) {
        sandboxResult.className = 'sandbox-result-box';
        sandboxResult.textContent = `Error: ${chrome.runtime.lastError.message}. Verify that the extension service worker is active.`;
        return;
      }

      sandboxResult.className = 'sandbox-result-box active';
      
      if (response && response.result) {
        sandboxResult.textContent = response.result;
        sandboxCopyBtn.style.display = 'inline-flex';

        // Increment local statistics
        await incrementStats(text);
      } else if (response && response.error) {
        sandboxResult.innerHTML = `<span style="color: #f87171; font-weight: 600;">API Error:</span> ${response.error}`;
      } else {
        sandboxResult.textContent = 'Could not generate a rewrite. Check your settings and API Key.';
      }
    });
  });

  sandboxCopyBtn.addEventListener('click', async () => {
    const resultText = sandboxResult.textContent.trim();
    if (resultText && !resultText.startsWith('Please enter') && !resultText.startsWith('Could not')) {
      try {
        await navigator.clipboard.writeText(resultText);
        sandboxCopyBtn.textContent = 'Copied!';
        setTimeout(() => {
          sandboxCopyBtn.textContent = 'Copy to Clipboard';
        }, 1500);
      } catch (err) {
        console.error('Failed to copy to clipboard:', err);
      }
    }
  });

  // --- 7. Statistics Management ---
  async function incrementStats(originalText) {
    const stats = await chrome.storage.local.get({
      statsTotalChecked: 0,
      statsVerbsUpgraded: 0
    });

    const newTotal = stats.statsTotalChecked + 1;
    let newVerbs = stats.statsVerbsUpgraded;

    // Check if the original draft had weak verbs
    const lowercase = originalText.toLowerCase();
    const hasWeakVerb = WEAK_VERBS_LIST.some(verb => {
      const regex = new RegExp(`\\b${verb}\\b`, 'i');
      return regex.test(lowercase);
    });

    if (hasWeakVerb) {
      newVerbs += 1;
    }

    await chrome.storage.local.set({
      statsTotalChecked: newTotal,
      statsVerbsUpgraded: newVerbs
    });
  }

  async function loadAndDisplayStats() {
    const stats = await chrome.storage.local.get({
      statsTotalChecked: 0,
      statsVerbsUpgraded: 0
    });

    statsTotalChecked.textContent = stats.statsTotalChecked;
    statsVerbsUpgraded.textContent = stats.statsVerbsUpgraded;

    // Calculate dynamic formatting score
    // Every bullet scanned adds 8 points, every verb upgraded adds 15 points, max 100%
    const rawScore = (stats.statsTotalChecked * 8) + (stats.statsVerbsUpgraded * 15);
    const score = Math.min(100, rawScore || 0);

    statsScorePercent.textContent = `${score}%`;
    statsProgressFill.style.width = `${score}%`;
  }
});
