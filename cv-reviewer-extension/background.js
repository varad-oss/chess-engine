/**
 * CV Reviewer Service Worker (background.js)
 * Handles secure API requests to Gemini / OpenAI.
 */

const DEFAULT_SYSTEM_PROMPT = `You are an expert technical resume writer and recruiter specializing in elite FAANG companies, top-tier quantitative finance firms, and investment banks (e.g., Goldman Sachs, Citadel).

Your task is to rewrite the user's resume bullet point strictly into the high-impact "Accomplished [X] as measured by [Y], by doing [Z]" format.

Core Constraints:
1. Focus on the result [X] first: Start the bullet point with a high-impact business outcome or engineering result (e.g., "Boosted database throughput", "Reduced page load time", "Streamlined client onboarding").
2. Quantify the impact [Y]: Provide a clear, measured metric (e.g., "by 45%", "saving $12,000/year", "for 50,000+ active users"). If the original bullet point does not contain a metric, create a realistic placeholder in brackets, e.g., "[by X%]" or "[saving $Y]" or "[for Z users]", and instruct the user to replace it.
3. Define the methodology [Z]: Detail the technical action, stack, or strategy used to achieve this result (e.g., "by implementing Redis query caching in Node.js", "by designing a multi-threaded C++ lock-free queue", "by refactoring legacy React state management").
4. Tone & Style: Keep it concise, action-oriented, and highly professional. Do NOT use pronouns (I, we, they) or weak verbs (helped, worked on).
5. Output format: Return ONLY the rewritten bullet point itself. Do not write introductory words, conversational filler, or wrap it in quotes. Return the plain text bullet.`;

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message.action === 'REWRITE_BULLET') {
    handleRewrite(message.bulletPoint, sendResponse);
    return true; // Keep message channel open for asynchronous sendResponse
  }
});

/**
 * Orchestrates LLM request based on configured provider.
 */
async function handleRewrite(bulletPoint, sendResponse) {
  try {
    const config = await chrome.storage.local.get({
      provider: 'gemini',
      apiKey: '',
      model: 'gemini-2.5-flash'
    });

    if (!config.apiKey) {
      sendResponse({ error: 'Missing API Key. Please click the extension icon to set your API key in the settings popup.' });
      return;
    }

    let rewrittenText = '';
    if (config.provider === 'gemini') {
      rewrittenText = await callGeminiAPI(bulletPoint, config.apiKey, config.model);
    } else if (config.provider === 'openai') {
      rewrittenText = await callOpenAIAPI(bulletPoint, config.apiKey, config.model);
    } else {
      throw new Error(`Unsupported LLM provider: ${config.provider}`);
    }

    sendResponse({ result: rewrittenText.trim() });
  } catch (error) {
    console.error('Error rewriting bullet point:', error);
    sendResponse({ error: error.message || 'An unexpected error occurred during the LLM API call.' });
  }
}

/**
 * Call Gemini API using fetch.
 */
async function callGeminiAPI(bulletPoint, apiKey, model) {
  const url = `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${apiKey}`;
  
  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      contents: [
        {
          role: 'user',
          parts: [{ text: `Bullet point to rewrite: "${bulletPoint}"` }]
        }
      ],
      systemInstruction: {
        parts: [{ text: DEFAULT_SYSTEM_PROMPT }]
      },
      generationConfig: {
        temperature: 0.2,
        maxOutputTokens: 200
      }
    })
  });

  if (!response.ok) {
    const errorData = await response.json().catch(() => ({}));
    const message = errorData.error?.message || `HTTP error ${response.status}`;
    throw new Error(`Gemini API Error: ${message}`);
  }

  const data = await response.json();
  const candidate = data.candidates?.[0];
  const text = candidate?.content?.parts?.[0]?.text;
  
  if (!text) {
    throw new Error('Gemini API returned an empty or invalid response structure.');
  }

  return text;
}

/**
 * Call OpenAI API using fetch.
 */
async function callOpenAIAPI(bulletPoint, apiKey, model) {
  const url = 'https://api.openai.com/v1/chat/completions';

  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${apiKey}`
    },
    body: JSON.stringify({
      model: model,
      messages: [
        { role: 'system', content: DEFAULT_SYSTEM_PROMPT },
        { role: 'user', content: `Bullet point to rewrite: "${bulletPoint}"` }
      ],
      temperature: 0.2,
      max_tokens: 200
    })
  });

  if (!response.ok) {
    const errorData = await response.json().catch(() => ({}));
    const message = errorData.error?.message || `HTTP error ${response.status}`;
    throw new Error(`OpenAI API Error: ${message}`);
  }

  const data = await response.json();
  const text = data.choices?.[0]?.message?.content;

  if (!text) {
    throw new Error('OpenAI API returned an empty or invalid response structure.');
  }

  return text;
}
