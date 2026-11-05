import { confirmAction, renderTasks } from './interactions.js';
/** 安装界面只展示宿主校验结果，文件选择、验签、信任和磁盘写入都由宿主完成。 */
export function setupPackages(client, changed, toast) {
  const find = id => document.getElementById(id);
  let refreshing = false, disposed = false, previewTask, manage, pending = false;
  let lastTerminal;
  let pluginSignature;
  let packId, lastPackTask;
  let advancingPack = false;
  let publisher, publisherTask;
  let appIcon, iconLoading = false;
  const iconInput = find('pack-app-icon');
  const iconPreview = find('pack-icon-preview');
  const iconStatus = find('pack-icon-status');
  const clearIcon = find('clear-pack-icon');
  iconInput.onchange = async () => {
    const file = iconInput.files[0];
    iconInput.value = '';
    if (!file || iconLoading) return;
    iconLoading = true;
    iconInput.disabled = clearIcon.disabled = true;
    try {
      if (!['image/png', 'image/jpeg', 'image/webp'].includes(file.type) || file.size > 8 * 1024 * 1024)
        throw Error('请选择不超过 8 MiB 的 PNG、JPEG 或 WebP。');
      const bitmap = await createImageBitmap(file);
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 96;
      try {
        const side = Math.min(bitmap.width, bitmap.height);
        canvas.getContext('2d').drawImage(bitmap, (bitmap.width - side) / 2, (bitmap.height - side) / 2, side, side, 0, 0, 96, 96);
      } finally { bitmap.close(); }
      const image = canvas.toDataURL('image/webp', .86);
      if (!image.startsWith('data:image/webp;base64,') || image.length > 16407) throw Error('图标内容过于复杂，请换一张图片。');
      appIcon = image.split(',')[1];
      iconPreview.src = image;
      iconPreview.hidden = clearIcon.hidden = false;
      iconStatus.textContent = '图标已准备好，将随应用包签名。';
    } catch (error) { iconStatus.textContent = error.message || '无法读取图标'; }
    finally { iconLoading = false; iconInput.disabled = clearIcon.disabled = false; }
  };
  clearIcon.onclick = () => {
    appIcon = undefined;
    iconPreview.removeAttribute('src');
    iconPreview.hidden = clearIcon.hidden = true;
    iconStatus.textContent = '已移除图标，将使用通用图标。';
  };
  const packStatus = find('pack-status');
  const manifestField = find('pack-manifest');
  const packFields = ['name', 'id', 'version', 'entry'].map(key => find(`pack-app-${key}`));
  const required = ['请输入应用名称。', '请输入应用标识。', '请输入版本号。', '请输入入口页面。'];
  const checks = [
    value => new TextEncoder().encode(value).length <= 128 && !/[\x00-\x1f\x7f]/.test(value),
    value => value.length <= 100 && !value.startsWith('org.alkaid.') && /^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$/.test(value),
    value => /^(0|[1-9]\d{0,5})(\.(0|[1-9]\d{0,5})){2}$/.test(value),
    value => value.length <= 240 && value.endsWith('.html') && value.split('/').every(part =>
      /^[a-z0-9_.-]{1,80}$/i.test(part) && !part.endsWith('.') &&
      !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)/i.test(part)),
  ];
  const hints = [
    '应用名称最多 128 个 UTF-8 字节，且不能包含控制字符。',
    '请输入小写点分标识，例如 com.example.myapp。每段以字母开头，org.alkaid. 为平台保留。',
    '请输入三段数字，例如 1.0.0。每段最多六位，且不能有前导零。',
    '请输入构建目录内的相对 .html 路径，例如 pages/index.html。',
  ];
  function validatePackField(field, index) {
    const value = field.value.trim();
    const message = !value ? required[index] : checks[index](value) ? '' : hints[index];
    find(`${field.id}-error`).textContent = message;
    field.setAttribute('aria-invalid', String(!!message));
    return !message;
  }
  for (const [index, field] of packFields.entries()) {
    const error = document.createElement('small');
    error.id = `${field.id}-error`;
    error.className = 'pack-field-error';
    error.setAttribute('aria-live', 'polite');
    field.after(error);
    field.setAttribute('aria-describedby', error.id);
    field.addEventListener('blur', () => validatePackField(field, index));
    field.addEventListener('input', () => {
      if (field.hasAttribute('aria-invalid')) validatePackField(field, index);
    });
  }
  const sharedManifest = { manifestVersion: 3, name: '我的应用', version: '1.0.0', publisher: { name: '开发者' } };
  const appManifest = { ...sharedManifest, type: 'app', appId: 'com.example.app',
    runtime: { apiMajor: 1, minVersion: '2.0.0' }, entry: 'index.html', routing: 'hash', instanceMode: 'single',
    window: { width: 1000, height: 720, minWidth: 640, minHeight: 480, resizable: true }, permissions: [], dependencies: [] };
  const pluginManifest = { ...sharedManifest, type: 'plugin', name: '我的插件', pluginId: 'com.example.plugin',
    protocolMajor: 1, architecture: 'win-x64', entry: 'worker.exe', capabilities: [], events: [] };
  let templateText = '';
  async function useTemplate(template) {
    if (manifestField.value && manifestField.value !== templateText && !await confirmAction('替换当前清单？', '当前高级清单中的修改会被模板覆盖。', '替换清单')) return;
    templateText = JSON.stringify(template, null, 2);
    manifestField.value = templateText;
  }
  useTemplate(appManifest);
  find('pack-app-template').addEventListener('click', () => useTemplate(appManifest));
  find('pack-plugin-template').addEventListener('click', () => useTemplate(pluginManifest));
  function showPublisher(value) {
    publisher = value;
    find('local-publisher-form').hidden = !!value;
    find('local-publisher-status').textContent = value ? `本地发布者：${value.name}` : '首次打包请创建本地发布者。';
    find('publisher-backup-hint').textContent = value
      ? '设置至少 12 个字符的备份口令。换电脑后需要备份文件和口令。'
      : '使用已有的备份文件和口令恢复原发布者。';
    find('submit-publisher-backup').textContent = value ? '保存加密备份' : '恢复发布者';
  }
  find('open-pack').addEventListener('click', async () => {
    find('pack-dialog').showModal();
    try { showPublisher(await client.apps.localPublisher()); }
    catch (error) { find('local-publisher-status').textContent = error.message; }
  });
  find('local-publisher-form').addEventListener('submit', async event => {
    event.preventDefault();
    const button = event.currentTarget.querySelector('button');
    button.disabled = true;
    try { showPublisher(await client.apps.localPublisher(find('local-publisher-name').value.trim())); }
    catch (error) { find('local-publisher-status').textContent = error.message; }
    finally { button.disabled = false; }
  });
  find('publisher-backup-form').addEventListener('submit', async event => {
    event.preventDefault();
    const button = find('submit-publisher-backup');
    const input = find('publisher-backup-password');
    button.disabled = true;
    try {
      const request = client.apps.publisherBackup(input.value, !publisher);
      input.value = '';
      publisherTask = (await request).taskId;
    } catch (error) {
      packStatus.textContent = error.message;
      button.disabled = false;
    }
  });
  find('pack-advanced').addEventListener('toggle', event => {
    find('pack-app-fields').hidden = event.currentTarget.open;
  });
  function renderPack(task) {
    lastPackTask = task;
    const editing = !task;
    const running = task?.status === 'running';
    const succeeded = task?.status === 'succeeded';
    const waiting = task?.status === 'awaitingInput';
    const phase = task?.phase ?? 'info';
    find('pack-form-content').hidden = !editing;
    find('pack-review').hidden = editing;
    find('pack-back').hidden = editing;
    find('pack-back').disabled = running || advancingPack;
    find('submit-pack').disabled = running || advancingPack;
    find('submit-pack').hidden = succeeded;
    find('pack-result-actions').hidden = !succeeded;
    for (const step of document.querySelectorAll('[data-pack-step]'))
      step.classList.toggle('active', step.dataset.packStep === (succeeded ? 'ready' : phase === 'key' ? 'source' : phase));
    if (editing) {
      find('submit-pack').textContent = '下一步 · 选择构建目录';
      packStatus.textContent = '选择已经构建好的目录，其中应包含应用入口文件。';
      return;
    }
    const steps = {
      source: ['选择构建目录', '请选择包含入口文件的构建产物目录。取消选择后可以在这里重试。', '选择构建目录'],
      key: ['选择签名密钥', '高级模式使用你自己的 PEM 私钥签名。选好后再确认保存位置。', '选择签名密钥'],
      output: ['选择安装包保存位置', '构建目录已选好。下一步选择生成的 .ypkg 文件保存在哪里，这里不会上传文件。', '选择保存位置'],
      ready: ['确认并生成安装包', '请核对下面的保存位置。点击生成后开始压缩、签名并验证安装包。', '生成安装包'],
    };
    const [title, description, button] = succeeded
      ? ['安装包已生成', '安装包已通过校验，可以打开所在文件夹，或在瑶光台安装试用。', '完成']
      : waiting ? (steps[phase] ?? steps.source)
      : running ? ['正在处理', '你可以关闭此面板，稍后从顶部的任务入口查看进度。', '处理中…']
      : ['本次打包已结束', '应用信息已保留，可以返回编辑后重新打包。', '返回编辑'];
    find('pack-review-step').textContent = succeeded ? '完成' : waiting ? '下一步' : running ? '进行中' : '任务结果';
    find('pack-review-title').textContent = title;
    find('pack-review-description').textContent = description;
    find('pack-source-name').textContent = task.sourceName || '尚未选择';
    find('pack-review-app').textContent = [task.packageName, task.packageVersion && `v${task.packageVersion}`, task.appId, task.publisherName].filter(Boolean).join(' · ') || find('pack-app-name').value;
    find('pack-output-path').textContent = task.outputPath || '尚未选择';
    find('submit-pack').textContent = button;
    packStatus.textContent = task.message;
  }
  async function backToPackForm() {
    if (advancingPack || lastPackTask?.status === 'running') return;
    advancingPack = true;
    try {
      if (lastPackTask?.status === 'awaitingInput') await client.apps.cancelPack(packId);
      packId = undefined;
      lastPackTask = undefined;
    } catch (error) { toast(error.message); }
    finally { advancingPack = false; renderPack(lastPackTask); await refresh(); }
  }
  find('pack-back').addEventListener('click', backToPackForm);
  find('pack-reveal').addEventListener('click', () => client.apps.revealPack(packId).catch(error => toast(error.message)));
  find('pack-install').addEventListener('click', async event => {
    event.currentTarget.disabled = true;
    try { await client.apps.installPacked(packId); find('pack-dialog').close(); }
    catch (error) { toast(error.message); }
    finally { find('pack-install').disabled = false; await refresh(); }
  });
  renderPack();
  find('close-pack').addEventListener('click', () => find('pack-dialog').close());
  find('submit-pack').addEventListener('click', async () => {
    if (advancingPack) return;
    if (lastPackTask) {
      if (lastPackTask.status !== 'awaitingInput') { await backToPackForm(); return; }
      advancingPack = true;
      renderPack(lastPackTask);
      try { await client.apps.advancePack(packId); }
      catch (error) { toast(error.message); }
      finally { advancingPack = false; await refresh(); }
      return;
    }
    const advanced = find('pack-advanced').open;
    if (!advanced && iconLoading) { packStatus.textContent = '正在处理应用图标，请稍等。'; return; }
    const local = !advanced || !find('pack-manual-key').checked;
    if (!advanced) {
      const invalid = packFields.filter((field, index) => !validatePackField(field, index));
      if (invalid.length) {
        packStatus.textContent = '请修正表单中标出的项目。';
        invalid[0].focus();
        return;
      }
    }
    if (local && !publisher) {
      packStatus.textContent = '请先创建本地发布者。';
      find('local-publisher-name').focus();
      return;
    }
    let manifest = manifestField.value;
    if (!advanced) {
      const [name, appId, version, entry] = packFields.map(field => field.value.trim());
      const permissions = [...document.querySelectorAll('input[name="pack-permission"]:checked')].map(option => option.value);
      manifest = JSON.stringify({ ...appManifest, name, appId, version, entry, permissions, ...(appIcon ? { icon: appIcon } : {}) });
    } else {
      try { JSON.parse(manifest); }
      catch {
        packStatus.textContent = '高级清单的 JSON 格式有误，请检查引号、逗号和括号。';
        manifestField.focus();
        return;
      }
    }
    packId = undefined;
    find('submit-pack').disabled = true;
    packStatus.textContent = '正在打开资源目录选择器…';
    try {
      const { taskId } = await client.apps.preparePack(manifest, local, true);
      packId = taskId;
    } catch (error) {
      packStatus.textContent = error.message;
      find('submit-pack').disabled = false;
      if (advanced && error.code === 'INVALID_REQUEST') manifestField.focus();
    }
    await refresh();
  });
  function preview(task) {
    const info = task.preview;
    if (previewTask?.taskId === task.taskId && find('install-dialog').open) return;
    previewTask = task;
    const plugin = info.type === 'plugin';
    find('install-eyebrow').textContent = plugin ? '插件安装' : '应用安装';
    find('install-title').textContent = plugin ? '确认插件信息' : '确认应用信息';
    find('package-kind').textContent = plugin ? '插件' : '应用';
    find('package-capability-label').textContent = plugin ? '声明能力' : '访问权限';
    find('package-name').textContent = `${info.name} (${plugin ? info.pluginId : info.appId})`;
    find('package-version').textContent = info.currentVersion ? `${info.currentVersion} → ${info.version}` : info.version;
    find('package-publisher').textContent = info.publisherKeyId;
    const labels = { 'storage.kv': '保存应用私有设置', 'files.private': '读写应用私有文本',
      'files.userSelected.read': '读取你通过系统对话框选定的文件', 'files.userSelected.write': '写入你通过系统对话框确认的保存目标', 'net.request': '通过瑶光台访问已授权的网络端点' };
    for (const endpoint of info.networkEndpoints ?? []) labels[endpoint.scope] = `${endpoint.id}：${endpoint.baseUrl} · ${endpoint.methods.join('/')} · ${endpoint.allowPrivateNetwork ? '允许内网及本机访问' : '仅公网'}`;
    find('package-permissions').textContent = plugin
      ? `方法：${info.capabilities.join('、') || '无'} · 事件：${info.events.join('、') || '无'}`
      : info.permissions.length
      ? info.permissions.map(name => `${labels[name] ?? name}${info.addedPermissions?.includes(name) ? '（新增授权）' : ''}`).join('；')
      : '仅查询自身运行信息与可用能力';
    find('package-trust-note').textContent = info.publisherTrusted
      ? '该发布者已受信任。更新仍会校验发布者身份与全部资源。'
      : '这是首次遇到的发布者。请从你确认可信的渠道核对指纹，再决定是否安装。';
    find('trust-row').hidden = info.publisherTrusted;
    find('trust-publisher').checked = false;
    find('confirm-install').disabled = !info.publisherTrusted;
    find('confirm-install').textContent = info.currentVersion ? '确认更新' : '确认安装';
    find('package-confirm-error').hidden = true;
    if (!find('install-dialog').open) find('install-dialog').showModal();
  }
  async function refreshPlugins() {
    try {
      const plugins = await client.plugins.list();
      const signature = JSON.stringify(plugins);
      if (signature !== pluginSignature) {
        const rows = plugins.map(plugin => {
          const row = document.createElement('article');
          row.className = 'app-row';
          const details = document.createElement('div');
          const title = document.createElement('strong');
          title.textContent = plugin.name;
          const meta = document.createElement('small');
          meta.textContent = `${plugin.pluginId} · v${plugin.version}${plugin.error ? ` · ${plugin.error}` : ''}`;
          details.append(title, meta);
          const remove = document.createElement('button');
          remove.className = 'text-button';
          remove.textContent = '卸载';
          Object.assign(remove.dataset, { packageAction: 'uninstall', packageKind: 'plugin', packageId: plugin.pluginId, packageName: plugin.name });
          row.append(details, remove);
          return row;
        });
        find('plugin-list').replaceChildren(...rows);
        find('plugin-count').textContent = `${plugins.length} 个插件`;
        pluginSignature = signature;
      }
      find('plugin-status').textContent = plugins.length ? '安装同一插件的新版本即可更新。' : '还没有安装插件。';
      find('plugin-status').classList.remove('error');
    } catch (error) {
      find('plugin-status').textContent = error.message;
      find('plugin-status').classList.add('error');
    }
  }
  async function refresh() {
    if (refreshing || disposed) return;
    refreshing = true;
    try {
      const tasks = await client.apps.listTasks();
      if (disposed) return;
      const backupTask = tasks.find(task => task.taskId === publisherTask);
      if (backupTask) {
        packStatus.textContent = backupTask.message;
        if (backupTask.status !== 'running') {
          publisherTask = undefined;
          find('submit-publisher-backup').disabled = false;
          if (backupTask.status === 'succeeded') showPublisher(await client.apps.localPublisher());
        }
      }
      renderTasks(tasks, task => {
        packId = task.taskId;
        renderPack(task);
        if (!find('pack-dialog').open) find('pack-dialog').showModal();
      });
      if (!packId) packId = tasks.find(task => task.kind === 'pack' && ['running', 'awaitingInput'].includes(task.status))?.taskId;
      const packTask = tasks.find(task => task.taskId === packId);
      if (packTask) {
        renderPack(packTask);
      }
      const active = tasks.find(task => ['running', 'awaitingInput', 'awaitingConsent'].includes(task.status));
      const latest = active ?? tasks[0];
      find('install-package').disabled = !!active || pending;
      find('install-plugin').disabled = !!active || pending;
      if (latest) {
        find('package-task').textContent = latest.message;
        find('package-task').classList.toggle('error', latest.status === 'failed');
        if (latest.status === 'awaitingConsent') preview(latest);
        if (['succeeded', 'failed', 'cancelled'].includes(latest.status) && lastTerminal !== latest.taskId) {
          lastTerminal = latest.taskId;
          await changed();
        }
      } else {
        find('package-task').textContent = '选择 YPKG 应用包，校验后确认安装。';
        find('package-task').classList.remove('error');
      }
      if (!find('panel-plugins').hidden) {
        await refreshPlugins();
        if (latest?.kind.startsWith('plugin')) {
          find('plugin-status').textContent = latest.message;
          find('plugin-status').classList.toggle('error', latest.status === 'failed');
        }
      }
    } catch (error) {
      find('package-task').textContent = `包管理状态暂不可用：${error.message}`;
      find('package-task').classList.add('error');
    } finally { refreshing = false; }
  }
  find('install-package').addEventListener('click', async () => {
    pending = true; find('install-package').disabled = true;
    try { await client.apps.prepareInstall(); }
    catch (error) { toast(`无法选择应用包：${error.message}`); }
    finally { pending = false; await refresh(); }
  });
  document.querySelector('[data-open="plugins"]').addEventListener('click', () => void refresh());
  find('install-plugin').addEventListener('click', async () => {
    if (pending) return;
    pending = true; find('install-plugin').disabled = true;
    try { await client.plugins.prepareInstall(); }
    catch (error) { toast(error.message); }
    finally { pending = false; await refresh(); }
  });
  find('trust-publisher').addEventListener('change', () => {
    find('confirm-install').disabled = !previewTask?.preview.publisherTrusted && !find('trust-publisher').checked;
  });
  async function cancel() {
    if (!previewTask || pending) return;
    pending = true;
    try { await client.apps.cancelInstall(previewTask.taskId); find('install-dialog').close(); previewTask = undefined; }
    catch (error) { toast(error.message); }
    finally { pending = false; await refresh(); }
  }
  find('cancel-install').addEventListener('click', cancel);
  find('install-dialog').addEventListener('cancel', event => { event.preventDefault(); cancel(); });
  find('confirm-install').addEventListener('click', async () => {
    if (!previewTask || pending) return;
    pending = true; find('confirm-install').disabled = true; find('cancel-install').disabled = true;
    try {
      await client.apps.confirmInstall(previewTask.taskId, find('trust-publisher').checked);
      find('install-dialog').close(); previewTask = undefined;
    } catch (error) {
      find('package-confirm-error').textContent = error.message;
      find('package-confirm-error').hidden = false;
    } finally {
      pending = false; find('cancel-install').disabled = false;
      find('confirm-install').disabled = !previewTask?.preview.publisherTrusted && !find('trust-publisher').checked;
      await refresh();
    }
  });
  document.addEventListener('click', event => {
    const button = event.target.closest('[data-package-action]');
    if (!button || button.disabled) return;
    manage = { action: button.dataset.packageAction, appId: button.dataset.packageId, plugin: button.dataset.packageKind === 'plugin' };
    find('uninstall-data-option').hidden = manage.plugin || manage.action !== 'uninstall';
    find('uninstall-clear-data').checked = false;
    updateManageConfirmation();
    find('manage-title').textContent = manage.action === 'uninstall' ? `卸载 ${button.dataset.packageName}？` : '恢复上一版本？';
    find('manage-description').textContent = manage.plugin
      ? '请先关闭依赖此插件的应用。移除后，缺少必需插件的应用将无法启动。'
      : manage.action === 'uninstall'
      ? '默认保留应用数据。以后重新安装时仍需匹配原发布者。'
      : '请先关闭应用。恢复只切换程序版本，应用数据保持不变。';
    find('manage-dialog').showModal();
  });
  function updateManageConfirmation() {
    const deletesData = !find('uninstall-data-option').hidden && find('uninstall-clear-data').checked;
    const confirm = find('confirm-manage');
    confirm.textContent = deletesData ? '删除数据并卸载' : '确认';
    confirm.classList.toggle('btn-danger', deletesData);
    confirm.classList.toggle('btn-primary', !deletesData);
  }
  find('uninstall-clear-data').addEventListener('change', updateManageConfirmation);
  find('cancel-manage').addEventListener('click', () => find('manage-dialog').close());
  find('confirm-manage').addEventListener('click', async () => {
    if (!manage || pending) return;
    pending = true; find('confirm-manage').disabled = true;
    try {
      if (manage.plugin) await client.plugins.uninstall(manage.appId);
      else if (manage.action === 'uninstall') await client.apps.uninstall(manage.appId, { clearData: find('uninstall-clear-data').checked });
      else await client.apps.rollback(manage.appId);
      find('manage-dialog').close();
    }
    catch (error) { toast(error.message); }
    finally { pending = false; find('confirm-manage').disabled = false; await refresh(); }
  });
  const timer = setInterval(refresh, 1000);
  refresh();
  return () => { disposed = true; clearInterval(timer); };
}
