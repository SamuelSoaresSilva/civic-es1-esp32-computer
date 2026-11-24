const fs = require('node:fs');
const path = require('node:path');

const project = JSON.parse(fs.readFileSync(path.join(__dirname, 'boot-projeto.json'), 'utf8'));
const { width, height, frames } = project;
if (project.version !== 1 || width !== 256 || height !== 64 ||
    !Array.isArray(frames) || frames.length === 0) {
  throw new Error('O projeto precisa ter pelo menos um quadro de 256 × 64.');
}

const frameBytes = width * height / 2;
let previous = Buffer.alloc(frameBytes);
const data = [];
const durations = [];
for (const { pixels, duration } of frames) {
  if (typeof pixels !== 'string' || pixels.length !== frameBytes * 2 || /[^0-9a-f]/i.test(pixels) ||
      !Number.isInteger(duration) || duration < 20 || duration > 60000) {
    throw new Error('Quadro inválido no projeto.');
  }
  const next = Buffer.from(pixels, 'hex');
  durations.push(duration);

  for (let start = 0; start < frameBytes;) {
    const changed = next[start] !== previous[start];
    let end = start + 1;
    while (end < frameBytes && end - start < 128 && (next[end] !== previous[end]) === changed) {
      end++;
    }
    data.push((changed ? 0x80 : 0) | (end - start - 1));
    if (changed) data.push(...next.subarray(start, end));
    start = end;
  }
  previous = next;
}

const rows = [];
for (let i = 0; i < data.length; i += 16) {
  rows.push('    ' + data.slice(i, i + 16).map(byte => '0x' + byte.toString(16).padStart(2, '0')).join(', ') + ',');
}

const header = [
  '// Gerado por node animacao/gerar-boot.cjs a partir de animacao/boot-projeto.json.',
  '#pragma once',
  '#include <Arduino.h>',
  '',
  `constexpr size_t BOOT_FRAME_BYTES = ${frameBytes};`,
  `const uint16_t bootDurationMs[] PROGMEM = { ${durations.join(', ')} };`,
  '// Bit 7 indica bytes novos. Os outros bits guardam o comprimento menos um.',
  'const uint8_t bootDelta[] PROGMEM = {',
  ...rows,
  '};',
  ''
].join('\n');
fs.writeFileSync(path.join(__dirname, '..', 'painel_oled', 'boot_animacao.h'), header);
console.log(`${frames.length} quadros, ${frames.length * frameBytes} bytes originais → ${data.length} bytes comprimidos.`);
