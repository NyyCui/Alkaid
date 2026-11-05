/** 桌面负责窗口外观和布局，应用页面由宿主独立视图承载。 */
export function setupWorkspace(client, refresh, toast) {
  const find = id => document.getElementById(id);
  const frame = find('app-workspace');
  const content = find('workspace-content');
  const handle = find('workspace-handle');
  const grip = find('workspace-resize');
  const blockers = [...document.querySelectorAll('dialog, .workspace-panel, .system-menu')];
  let app, layout, maximized = false, drag, pending, lastSent, sending = false, minimizing;
  let exitPreview;
  function animateExit(kind) {
    if (frame.hidden || matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    exitPreview?.remove();
    const box = frame.getBoundingClientRect();
    const dock = [...document.querySelectorAll('.app-dock')].find(button => button.dataset.appId === app.appId);
    const target = dock?.getBoundingClientRect();
    const preview = document.createElement('div');
    preview.className = 'workspace-exit-preview';
    preview.dataset.motion = kind;
    preview.setAttribute('aria-hidden', 'true');
    preview.textContent = app.name;
    Object.assign(preview.style, { left: `${box.left}px`, top: `${box.top}px`, width: `${box.width}px`, height: `${box.height}px` });
    document.body.append(preview);
    exitPreview = preview;
    const dx = target ? target.left + target.width / 2 - box.left - box.width / 2 : 0;
    const dy = (target ? target.top + target.height / 2 : innerHeight - 25) - box.top - box.height / 2;
    const animation = preview.animate(kind === 'minimize'
      ? [{ opacity: .92, transform: 'none' }, { opacity: 0, transform: `translate(${dx}px, ${dy}px) scale(.08)` }]
      : [{ opacity: .75, transform: 'none' }, { opacity: 0, transform: 'translateY(9px) scale(.95)' }],
    { duration: kind === 'minimize' ? 280 : 170, easing: 'cubic-bezier(.25,.7,.35,1)', fill: 'forwards' });
    animation.finished.then(() => preview.remove(), () => preview.remove());
  }
  const menuHeight = document.querySelector('.menubar').offsetHeight;
  const titleHeight = parseInt(getComputedStyle(frame).getPropertyValue('--workspace-title-height'), 10);
  const area = () => ({ left: 12, top: menuHeight + 4, right: Math.max(13, innerWidth - 12),
    bottom: Math.max(menuHeight + 5, innerHeight - (maximized ? 92 : 4)) });
  const clamp = (value, min, max) => Math.max(min, Math.min(value, max));
  function place() {
    const { left, top, right, bottom } = area();
    const width = maximized ? right - left : clamp(layout.width, Math.min(app.workspaceBounds.minWidth, right - left), right - left);
    const height = maximized ? bottom - top : clamp(layout.height, Math.min(app.workspaceBounds.minHeight, bottom - top), bottom - top);
    const x = maximized ? left : clamp(layout.x, left, right - width);
    // 与桌面面板一样，允许窗口底部越过 Dock，标题栏留在可拖动范围内。
    const y = maximized ? top : clamp(layout.y, top, Math.max(top, innerHeight - 110));
    Object.assign(frame.style, { left: `${x}px`, top: `${y}px`, width: `${width}px`, height: `${height}px` });
  }
  async function send() {
    if (sending) return;
    sending = true;
    try {
      while (pending) {
        const next = pending;
        pending = undefined;
        try { await client.apps.layoutWorkspace(next); }
        catch (error) {
          if (next.minimize) minimizing = undefined;
          if (error.code !== 'APP_NOT_RUNNING') toast(error.message);
        }
        if (next.minimize) await refresh();
      }
    } finally { sending = false; }
  }
  function sync({ focus = false, persist = false, minimize = false } = {}) {
    if (!app || (minimizing === app.appId && !minimize)) return;
    const visible = !minimize && !blockers.some(element => element.matches('dialog[open], .workspace-panel:not([hidden]), .system-menu[open]'));
    frame.hidden = !visible;
    if (visible) place();
    const rect = content.getBoundingClientRect();
    const next = { appId: app.appId, x: Math.round(rect.x), y: Math.round(rect.y),
      width: Math.round(rect.width), height: Math.round(rect.height), visible, focus, persist, minimize };
    const signature = JSON.stringify(next);
    if (signature === lastSent) return;
    lastSent = signature;
    pending = next;
    void send();
  }
  const observer = new MutationObserver(() => sync());
  for (const element of blockers) observer.observe(element, { attributes: true, attributeFilter: ['hidden', 'open'] });
  const resize = () => sync();
  const hideClosed = event => {
    if (event.detail?.event !== 'workspace.hidden' || event.detail.payload?.appId !== app?.appId) return;
    animateExit('close');
    app = undefined;
    frame.hidden = true;
    lastSent = undefined;
    void refresh();
  };
  addEventListener('alkaid:runtime-event', hideClosed);
  addEventListener('resize', resize);
  addEventListener('pagehide', () => {
    observer.disconnect();
    removeEventListener('alkaid:runtime-event', hideClosed);
    removeEventListener('resize', resize);
  }, { once: true });
  function update(apps) {
    const next = apps.find(item => item.workspaceActive && item.workspaceBounds);
    if (!next) { app = undefined; minimizing = undefined; frame.hidden = true; lastSent = undefined; return; }
    const changed = next.appId !== app?.appId;
    app = next;
    if (next.state === 'closing') {
      for (const panel of blockers.filter(element => element.matches('.workspace-panel:not([hidden])')))
        panel.querySelector('[data-close]')?.click();
    }
    if (changed) {
      exitPreview?.remove();
      const saved = next.workspaceBounds;
      const space = area();
      // 持久化的是应用内容区域，恢复时加回标题栏和边框。
      const width = saved.width + 4, height = saved.height + titleHeight + 12;
      layout = { x: saved.x ? saved.x - 2 : Math.round((space.left + space.right - width) / 2),
        y: saved.y ? saved.y - titleHeight - 2 : Math.round((space.top + space.bottom - height) / 2), width, height };
      maximized = false;
      find('workspace-title').textContent = next.name;
      find('workspace-maximize').disabled = !saved.resizable;
      grip.hidden = !saved.resizable;
    }
    sync({ focus: changed });
  }
  find('workspace-minimize').onclick = () => {
    if (!app) return;
    animateExit('minimize');
    minimizing = app.appId;
    sync({ minimize: true });
  };
  find('workspace-maximize').onclick = () => {
    maximized = !maximized;
    grip.hidden = maximized || !app.workspaceBounds.resizable;
    sync({ focus: true });
  };
  find('workspace-close').onclick = async () => {
    if (!app) return;
    try { await client.apps.close(app.appId); await refresh(); }
    catch (error) { toast(error.message); }
  };
  for (const target of [handle, grip]) {
    target.addEventListener('pointerdown', event => {
      if (event.button !== 0 || event.target.closest('button') || !app || maximized) return;
      const rect = frame.getBoundingClientRect();
      drag = { appId: app.appId, x: event.clientX, y: event.clientY,
        bounds: { x: rect.x, y: rect.y, width: rect.width, height: rect.height } };
      target.setPointerCapture(event.pointerId);
    });
    target.addEventListener('pointermove', event => {
      if (!drag || drag.appId !== app?.appId) return;
      const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
      const { bounds } = drag;
      layout = target === handle ? { ...bounds, x: bounds.x + dx, y: bounds.y + dy }
        : { ...bounds, width: bounds.width + dx, height: bounds.height + dy };
      sync();
    });
    const finish = () => {
      if (drag?.appId === app?.appId && drag) sync({ focus: true, persist: true });
      drag = undefined;
    };
    target.addEventListener('pointerup', finish);
    target.addEventListener('lostpointercapture', finish);
  }
  return update;
}
