#!/usr/bin/env node
// Checks the browser demo's JavaScript move generator (frontend/index.html)
// against the same perft reference counts as the C++ core. The frontend uses
// it only to highlight legal moves; the engine server re-validates every move.
//
// Usage: node tests/integration/frontend_perft.js [max_nodes=100000]
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const html = fs.readFileSync(path.join(root, 'frontend', 'index.html'), 'utf8');
const src = html.slice(html.indexOf('class ChessGame'), html.indexOf('// UI & Game Controller State'));
const prelude = "const COLUMNS = ['a','b','c','d','e','f','g','h']; const RANKS = ['1','2','3','4','5','6','7','8'];\n";
const ChessGame = new Function(prelude + src + '\nreturn ChessGame;')();
const maxNodes = Number(process.argv[2] || 100000);

function perft(fen, depth) {
  const g = new ChessGame();
  g.loadFen(fen);
  const moves = g.generateLegalMoves();
  if (depth === 1) return moves.length;
  let n = 0;
  for (const m of moves) {
    const child = new ChessGame();
    child.loadFen(fen);
    child.makeMove(m);
    n += perft(child.generateFen(), depth - 1);
  }
  return n;
}

const suite = fs.readFileSync(path.join(root, 'tests', 'data', 'perft.epd'), 'utf8')
  .split('\n').filter(l => l.trim() && !l.startsWith('#'));
let pass = 0, fail = 0;
for (const line of suite) {
  const [fen, ...ops] = line.split(';').map(s => s.trim());
  for (const op of ops) {
    const [tag, count] = op.split(/\s+/);
    const depth = Number(tag.slice(1)), expected = Number(count);
    if (expected > maxNodes) continue;
    const got = perft(fen, depth);
    if (got === expected) pass++;
    else { fail++; console.log(`FAIL depth ${depth}: got ${got}, expected ${expected}  ${fen}`); }
  }
}
console.log(`frontend move generator: ${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
