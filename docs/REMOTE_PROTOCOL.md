# Giao thức điều khiển từ xa (v2)

Dùng giữa app Redstone (`src/remote/`) và CLI `redstonectl` (`cli/redstonectl`).

## Truyền tải và bảo mật

- TCP, mỗi dòng là một JSON object (JSON Lines, UTF-8, kết thúc bằng `\n`, tối đa 64 KB/dòng).
- App **chỉ lắng nghe `127.0.0.1`**. Muốn điều khiển máy khác, CLI mở đường hầm OpenSSH
  (`ssh -N -L 127.0.0.1:<local>:127.0.0.1:<port> user@host`); SSH lo mã hoá, kiểm tra host key và đăng nhập.
- Mặc định **tắt**. Bật trong *Cài đặt > Điều khiển từ xa*.
- Mỗi **tài khoản** gồm: tên, token (256-bit), vai trò, và (tuỳ chọn) danh sách server được phép.
- Xác thực thách-đáp: token không bao giờ đi qua kết nối, chỉ có HMAC của nó.
- Giới hạn: tối đa 8 phiên, 10 giây để xác thực, 5 lần sai liên tiếp thì từ chối kết nối mới 30 giây.
- Mọi lệnh đều được ghi vào `data/remote_audit.log` (tự xoay vòng ở 1 MB).

## Bắt tay

```
S -> {"type":"hello","app":"Redstone","protocol":2,"nonce":"<32 hex>"}
C -> {"cmd":"auth","user":"<tài khoản>","lang":"vi|en","proof":"<hex HMAC-SHA256(key=token, msg=nonce)>"}
S -> {"type":"auth","ok":true,"user":"<tài khoản>","role":"viewer|operator|admin"}
S -> {"type":"auth","ok":false,"error":"..."}        (rồi đóng kết nối)
```

`user` mặc định `admin` (tương thích bản v1 một token). `lang` quyết định ngôn ngữ của thông báo lỗi trả về trong phiên.
Tên tài khoản không tồn tại và token sai cho cùng một phản hồi và cùng thời gian xử lý.

## Phân quyền (vai trò)

Mỗi vai trò gồm cả quyền của vai trò thấp hơn. App kiểm tra **trước khi** tra cứu/thi hành lệnh.

| vai trò | được chạy |
|---|---|
| `viewer` | `ping` `whoami` `app` `list` `status` `console` `players` `backups` `props` `subscribe` `unsubscribe` |
| `operator` | + `start` `stop` `restart` `player` (op, kick, ban, whitelist, gamemode, msg, say...) `backup` |
| `admin` | + `send` (lệnh console thô) `prop-set` `audit` |

**Phạm vi server**: tài khoản có thể bị giới hạn trong một số server. Server ngoài phạm vi bị ẩn khỏi `list`,
bị từ chối ở mọi lệnh và không phát sự kiện cho phiên đó.

Từ chối quyền:

```
S -> {"id":7,"ok":false,"code":"forbidden","required":"operator","error":"..."}
```

(`required` chỉ có khi thiếu vai trò; vi phạm phạm vi server chỉ có `code:"forbidden"`.)

## Yêu cầu / phản hồi

```
C -> {"id":7,"cmd":"status","server":"Survival"}
S -> {"id":7,"ok":true,"data":{...}}
S -> {"id":7,"ok":false,"error":"..."}
```

`server` là id hoặc tên (không phân biệt hoa/thường; trùng tên thì dùng id).

| cmd | tham số | `data` |
|---|---|---|
| `ping` | - | `{pong, protocol}` |
| `whoami` | - | `{user, role, allServers, servers[], commands[], matrix{viewer,operator,admin}, protocol}` |
| `app` | - | `{name, version, protocol, language, uptimeSeconds, servers, serversRunning, playersOnline, sessions}` (chỉ tính server trong phạm vi) |
| `list` | - | `{servers:[{id,name,type,state,port,playersOnline}]}` |
| `status` | server | chi tiết + `players[]`, `autoRestart`, `autoBackup`, `autoBackupIntervalMinutes`, `backupRetention`, `stopTimeoutSeconds`, `autoStartOnAppLaunch` |
| `start` | server | `{state}` (đồng bộ, chỉ khi Stopped/Crashed) |
| `stop` / `restart` | server | `{accepted:true}` (chạy nền; hỏi lại bằng `status`) |
| `console` | server, `lines` (1-2000) | `{lines[]}` |
| `send` | server, `command` | `{sent}` (một dòng, không ký tự điều khiển, ≤512) |
| `players` | server | `{online[], whitelist[], ops[], banned[], bannedIps[]}` |
| `player` | server, `action`, `name`, `reason`/`mode`/`text` | `{sent}` |
| `backups` | server | `{backups:[{name,sizeBytes}]}` |
| `backup` | server | `{name}` (trả lời khi nén xong) |
| `props` | server | `{properties:{}}` (`rcon.password` bị che) |
| `prop-set` | server, `key`, `value` | `{key, restartRequired}` |
| `audit` | `lines` (1-500) | `{lines[]}` dòng dạng `thời gian \| tài khoản (vai trò) \| lệnh \| kết quả` |
| `subscribe` / `unsubscribe` | tuỳ chọn server | `{subscribed}` |

`player.action`: `op`, `deop`, `kick`, `ban`, `pardon`, `ban-ip`, `pardon-ip`, `whitelist-add`,
`whitelist-remove`, `whitelist-on`, `whitelist-off`, `whitelist-reload`, `gamemode`, `msg`, `say`.
Tên người chơi phải khớp `^[A-Za-z0-9_.]{1,32}$`; lý do/nội dung không chứa ký tự điều khiển
(chặn chèn thêm lệnh qua xuống dòng).

## Nhật ký truy cập

Mỗi dòng: `2026-10-29T12:00:00 | ops (operator) | restart Survival | ok`. Kết quả: `ok`, `started` (lệnh chạy nền),
`error: ...`, hoặc `DENIED (...)` khi bị từ chối quyền/phạm vi/xác thực. Giá trị khoá có chữ `password` được che bằng `***`.

## Sự kiện (sau `subscribe`)

```
{"type":"event","event":"console","server":"<id>","line":"..."}
{"type":"event","event":"state","server":"<id>","state":"Running"}
```

## Cố ý không có

Thêm/xoá server, khôi phục backup, sửa file tuỳ ý, cài mod/plugin, đổi cài đặt app, quản lý tài khoản, tắt app.
Tài khoản và vai trò chỉ chỉnh được trong chính app (*Cài đặt > Điều khiển từ xa*).
