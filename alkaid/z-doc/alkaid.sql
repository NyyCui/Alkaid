CREATE TABLE IF NOT EXISTS organization
(
    id            INTEGER NOT NULL,
    name          TEXT NOT NULL,-- 组织名称
    identifier    TEXT NOT NULL,-- 组织标识
    public_key    BLOB NOT NULL,-- 公钥
    private_key   BLOB NOT NULL,-- 私钥
    register_time TEXT NOT NULL,-- 注册时间
    PRIMARY KEY (id)
);

CREATE TABLE IF NOT EXISTS program
(
    id           INTEGER NOT NULL,
    org_id       INTEGER NOT NULL,-- 组织 Id
    identifier   TEXT    NOT NULL,-- 程序标识，组织内唯一
    icon         TEXT    NOT NULL,-- 图标
    name         TEXT    NOT NULL,-- 应用名称
    multiple     INTEGER NOT NULL,-- 是否多实例，0 单实例，1 多实例
    install_time TEXT    NOT NULL,-- 安装时间
    update_time TEXT NOT NULL,-- 更新时间
    PRIMARY KEY (id)
);
