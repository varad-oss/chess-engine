# Chrome Web Store Listing — CV Impact Reviewer

> Last Updated: 2026-06-27

## Store Listing

**Extension Name**  
CV Impact Reviewer

**Short Description**  
Real-time CV bullet point optimizer for FAANG, Quant, and Investment Banking roles.

**Detailed Description**  
CV Impact Reviewer is a real-time assistant that helps you write high-impact resume bullets directly inside university portals and application textareas.

Features:
- Identifies weak action verbs in real-time and suggests high-impact alternatives.
- Scans for missing quantification metrics and alerts you to add numbers/percentages.
- Highlights technology keywords and results to suggest smart bolding for scannability.
- Detects dangling words to prevent layout/spacing issues.
- Generates FAANG-optimized "Accomplished [X] as measured by [Y], by doing [Z]" bullet points using your own Gemini or OpenAI API keys.

How to use:
1. Click the extension icon to select your AI Provider (Gemini or OpenAI) and paste your API key.
2. Navigate to your university ERP portal or any application form.
3. Hover or focus on a textarea to reveal the floating "Analyze" button.
4. Click "Analyze" to inspect local recommendations and generate a professional FAANG-style rewrite.
5. Click "Insert" or "Copy" to apply the optimized bullet point instantly.

**Category**  
Productivity

**Single Purpose**  
Analyzes and rewrites resume bullet points inside web textareas to follow high-impact formats.

**Primary Language**  
English

## Graphics & Assets

| Asset | Dimensions | Status | Filename |
|-------|-----------|--------|----------|
| Store Icon | 128×128 PNG | ⬜ Not created | |
| Screenshot 1 | 1280×800 | ⬜ Not created | |

## Permissions Justification

| Permission | Type | Justification |
|------------|------|---------------|
| `storage` | permissions | Used to store the selected AI provider, model, local statistics, and API keys securely on the user's device. |
| `https://api.openai.com/*` | host_permissions | Used to send draft bullet points to the OpenAI Chat Completion API to generate professional rewrites. |
| `https://generativelanguage.googleapis.com/*` | host_permissions | Used to send draft bullet points to the Google Gemini Content Generation API to generate professional rewrites. |

## Privacy & Data Use

### Data Collection

**Does the extension collect user data?** Yes

| Data Type | Collected? | Transmitted Off-Device? | Purpose | Shared with Third Parties? |
|-----------|-----------|------------------------|---------|---------------------------|
| Authentication info | Yes | No | API keys are stored locally on-device. | No |
| Personal communications | Yes | Yes (to AI APIs) | Resume draft text is sent only to the configured LLM API. | No |

### Data Use Certification
- [x] Data is NOT sold to third parties
- [x] Data is NOT used for purposes unrelated to the extension's core functionality
- [x] Data is NOT used for creditworthiness or lending purposes

## Version History

| Version | Date | Changes | Status |
|---------|------|---------|--------|
| 1.0.0 | 2026-06-27 | Initial release. | Draft |
