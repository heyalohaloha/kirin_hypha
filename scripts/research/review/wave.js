(() => {
  'use strict';
  globalThis.HyphaReviewWave = class {
    constructor(canvas, onCursor) {
      this.canvas = canvas; this.onCursor = onCursor; this.cursor = 0; this.playhead = null; this.marks = [];
      canvas.addEventListener('pointerdown', event => {
        if (!this.item) return;
        const rect = canvas.getBoundingClientRect();
        this.setCursor(this.left + (event.clientX - rect.left - 12) / (rect.width - 24) * (this.right - this.left));
      });
      canvas.addEventListener('keydown', event => {
        if (!this.item || !['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
        event.preventDefault();
        this.setCursor(this.cursor + (event.key === 'ArrowLeft' ? -1 : 1) * this.item.rate * (event.shiftKey ? .02 : .002));
      });
      new ResizeObserver(() => this.draw()).observe(canvas);
    }
    load(item, marks) {
      this.item = item; this.marks = marks; this.playhead = null;
      [this.left, this.right] = item.preview; this.cursor = this.left; this.draw();
    }
    setCursor(sample) {
      this.cursor = Math.max(0, Math.min(this.item.frames - 1, Math.round(sample)));
      this.onCursor(this.cursor); this.draw();
    }
    zoom(factor) {
      const span = Math.min(this.item.frames, Math.max(this.item.rate * .2, (this.right - this.left) * factor));
      const centre = this.cursor >= this.left && this.cursor <= this.right ? this.cursor : (this.left + this.right) / 2;
      this.left = Math.max(0, Math.min(this.item.frames - span, centre - span / 2)); this.right = this.left + span; this.draw();
    }
    draw() {
      if (!this.item) return;
      const { canvas, item } = this;
      const width = Math.max(10, canvas.clientWidth), height = 190, dpr = Math.min(3, window.devicePixelRatio || 1);
      if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
        canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr);
      }
      const ctx = canvas.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, width, height);
      const x = sample => 12 + (sample - this.left) / (this.right - this.left) * (width - 24);
      ctx.save(); ctx.beginPath(); ctx.rect(12, 0, width - 24, height); ctx.clip();
      ctx.fillStyle = '#142d30'; ctx.fillRect(x(item.preview[0]), 0, x(item.preview[1]) - x(item.preview[0]), height - 27);
      ctx.strokeStyle = '#284148'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(12, 82); ctx.lineTo(width - 12, 82); ctx.stroke();
      const amplitude = 68 / Math.max(.05, item.peak);
      ctx.strokeStyle = '#8bbfb7'; ctx.beginPath();
      const pixels = Math.max(1, Math.floor(width - 24));
      for (let pixel = 0; pixel < pixels; pixel++) {
        const start = Math.max(0, Math.floor((this.left + pixel / pixels * (this.right - this.left)) / item.step));
        const end = Math.min(item.wave.length / 2, Math.ceil((this.left + (pixel + 1) / pixels * (this.right - this.left)) / item.step));
        let low = 0, high = 0;
        for (let bin = start; bin < end; bin++) { low = Math.min(low, item.wave[bin * 2]); high = Math.max(high, item.wave[bin * 2 + 1]); }
        ctx.moveTo(12 + pixel, 82 - high / 32767 * amplitude); ctx.lineTo(12 + pixel, 82 - low / 32767 * amplitude);
      }
      ctx.stroke();
      for (const [index, mark] of this.marks.entries()) {
        ctx.fillStyle = '#e2b57833'; ctx.strokeStyle = '#e7bc80';
        if (mark.end > mark.start) ctx.fillRect(x(mark.start), 28, x(mark.end) - x(mark.start), 125);
        ctx.beginPath(); ctx.moveTo(x(mark.start), 25); ctx.lineTo(x(mark.start), 154);
        if (mark.end > mark.start) { ctx.moveTo(x(mark.end), 25); ctx.lineTo(x(mark.end), 154); } ctx.stroke();
        ctx.fillStyle = '#f3d4a5'; ctx.font = '14px sans-serif'; ctx.fillText(String(index + 1), x(mark.start) + 3, 20);
      }
      for (const [position, colour] of [[this.cursor, '#f3d4a5'], [this.playhead, '#ffffff']]) {
        if (position === null) continue;
        ctx.strokeStyle = colour; ctx.beginPath(); ctx.moveTo(x(position), 0); ctx.lineTo(x(position), 160); ctx.stroke();
      }
      ctx.restore(); ctx.fillStyle = '#b0c0c5'; ctx.font = '14px sans-serif';
      const ticks = width < 500 ? 2 : 4;
      for (let n = 0; n <= ticks; n++) {
        const sample = this.left + (this.right - this.left) * n / ticks;
        ctx.textAlign = n === 0 ? 'left' : n === ticks ? 'right' : 'center';
        ctx.fillText((sample / item.rate).toFixed(this.right - this.left < item.rate ? 2 : 1) + ' s', x(sample), 183);
      }
    }
  };
})();
