/** 桌面共用交互，确认、任务与窗口操作保持可访问和可恢复。 */
export function confirmAction(title, description, label = '继续') {
  const dialog = document.getElementById('confirm-dialog');
  if (dialog.open) return Promise.resolve(false);
  document.getElementById('confirm-title').textContent = title;
  document.getElementById('confirm-description').textContent = description;
  document.getElementById('confirm-yes').textContent = label;
  dialog.returnValue = '';
  return new Promise(resolve => {
    document.getElementById('confirm-no').onclick = () => dialog.close('cancel');
    document.getElementById('confirm-yes').onclick = () => dialog.close('accept');
    dialog.addEventListener('close', () => resolve(dialog.returnValue === 'accept'), { once: true });
    dialog.showModal();
  });
}

export function renderTasks(tasks, resumePack) {
  const list = document.getElementById('task-list');
  const signature = JSON.stringify(tasks);
  if (list.dataset.signature === signature) return;
  list.dataset.signature = signature;
  const names = { pack: '生成安装包', install: '安装应用', pluginInstall: '安装插件', uninstall: '卸载应用',
    rollback: '恢复版本', permissions: '调整授权', clearData: '清理应用数据', clearBrowserData: '清理浏览器数据', publisherBackup: '发布者备份', pluginUninstall: '卸载插件' };
  const states = { running: '进行中', awaitingInput: '等待操作', awaitingConsent: '等待确认', succeeded: '已完成', failed: '失败', cancelled: '已取消' };
  const active = tasks.filter(task => ['running', 'awaitingInput', 'awaitingConsent'].includes(task.status));
  document.getElementById('task-count').textContent = active.length || '';
  list.replaceChildren(...tasks.map(task => {
    const row = document.createElement('article'); row.className = 'task-row';
    const title = document.createElement('strong'); title.textContent = names[task.kind] ?? '管理任务';
    const badge = document.createElement('span'); badge.className = `task-badge ${task.status}`; badge.textContent = states[task.status];
    const message = document.createElement('p'); message.textContent = task.message;
    row.append(title, badge, message);
    if (task.kind === 'pack') {
      const button = document.createElement('button'); button.className = 'text-button'; button.textContent = '查看打包任务';
      button.onclick = () => resumePack(task); row.append(button);
    }
    return row;
  }));
  if (!tasks.length) list.textContent = '暂无任务。安装和打包结果将显示在这里。';
}

export function setupExperience(client, showPanel, toast) {
  const find = id => document.getElementById(id);
  const context = find('app-context-menu');
  let contextTrigger;
  find('menu-exit').onclick = () => find('exit').click();
  find('host-minimize').onclick = () => client.window.minimize().catch(error => toast(error.message));
  find('host-maximize').onclick = () => client.window.toggleMaximize().catch(error => toast(error.message));
  document.querySelector('.menubar').addEventListener('dblclick', event => {
    if (!event.target.closest('button, summary, details')) find('host-maximize').click();
  });
  document.addEventListener('click', event => {
    for (const menu of document.querySelectorAll('details.system-menu[open], details.app-more[open]'))
      if (!menu.contains(event.target) || event.target.closest('button')) menu.open = false;
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
      for (const menu of document.querySelectorAll('details.system-menu[open], details.app-more[open]')) {
        const restoreFocus = menu.contains(document.activeElement);
        menu.open = false;
        if (restoreFocus) menu.querySelector('summary')?.focus();
      }
      if (!context.hidden) {
        const restoreFocus = context.contains(document.activeElement);
        context.hidden = true;
        if (restoreFocus) contextTrigger?.focus();
      }
    }
  });
  const filter = () => {
    const search = find('app-search').value.trim().toLocaleLowerCase();
    const state = find('app-filter').querySelector('input:checked').value;
    const rows = [...document.querySelectorAll('.managed-app')];
    for (const row of rows) row.hidden = !row.dataset.searchName.includes(search) ||
      (state === 'running' && !['running', 'starting', 'closing'].includes(row.dataset.state)) ||
      (state === 'stopped' && !['stopped', 'unavailable'].includes(row.dataset.state));
    find('search-empty').hidden = !rows.length || rows.some(row => !row.hidden);
  };
  find('app-search').addEventListener('input', filter);
  find('app-filter').addEventListener('change', filter);
  const observer = new MutationObserver(filter);
  observer.observe(find('apps-list'), { childList: true });
  addEventListener('pagehide', () => observer.disconnect(), { once: true });
  document.querySelector('.desktop').addEventListener('click', event => {
    for (const icon of document.querySelectorAll('.desktop-icon.selected')) icon.classList.remove('selected');
    event.target.closest('.desktop-icon')?.classList.add('selected');
  });
  document.addEventListener('contextmenu', event => {
    if (!event.target.closest('.desktop, .wallpaper')) return;
    const icon = event.target.closest('#desktop-apps [data-app-id]');
    contextTrigger = icon ?? document.activeElement;
    event.preventDefault();
    context.replaceChildren();
    const actions = icon ? [
      ['打开', () => client.apps.launch(icon.dataset.appId).catch(error => toast(error.message))],
      ['查看详情', () => {
        showPanel('apps');
        [...document.querySelectorAll('[data-app-detail]')].find(item => item.dataset.appDetail === icon.dataset.appId)?.click();
      }],
    ] : [
      ['打开应用管理', () => showPanel('apps')],
      ['刷新桌面', () => find('refresh').click()],
      ['更换壁纸…', () => {
        showPanel('settings');
        const chooser = find('choose-wallpaper');
        chooser.focus();
        chooser.click();
      }],
    ];
    for (const [label, action] of actions) {
      const button = document.createElement('button'); button.textContent = label;
      button.onclick = () => {
        context.hidden = true;
        action();
      };
      context.append(button);
    }
    context.hidden = false;
    context.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - context.offsetWidth - 6))}px`;
    context.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - context.offsetHeight - 6))}px`;
    context.querySelector('button').focus();
  });
  document.addEventListener('pointerdown', event => { if (!context.contains(event.target)) context.hidden = true; });
  // 业务执行期间通过取消按钮的状态约束 Esc，避免绕过正在提交的步骤。
  for (const dialog of document.querySelectorAll('dialog')) {
    dialog.addEventListener('cancel', event => {
      if (dialog.querySelector('button[id^="cancel-"]:disabled, button[id^="close-"]:disabled')) event.preventDefault();
    });
  }
}
