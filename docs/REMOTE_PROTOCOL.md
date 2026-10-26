# Giao thức điều khiển từ xa (v1)

Dùng giữa app Redstone (`src/remote/RemoteControlServer.*`) và CLI `redstonectl` (`cli/redstonectl`).

## Truyền tải và bảo mật

- TCP, mỗi dòng là một JSON object (JSON Lines, UTF-8, kết thúc bằng `\n`, tối đa 64 KB/dòng).
- App **chỉ lắng nghe `127.0.0.1`**. Muốn điều khiển máy khác, CLI mở đường hầm OpenSSH
  (`ssh -N -L 127.0.0.1:<local>:127.0.0.1:<port> user@host`) bằng lệnh `ssh` của hệ thống; SSH lo
  mã hoá, kiểm tra host key và đăng nhập. Không có cổng nào của app mở ra mạng.
- Mặc định **tắt**. Bật trong *Cài đặt > Điều khiển từ xa*; app tự sinh token 256-bit.
- Xác thực thách-đáp: token không bao giờ đi qua kết nối.
- Giới hạn: tối đa 8 phiên đồng thời, 10 giây để xác thực, 5 lần sai liên tiếp thì từ chối kết nối mới 30 giây.

## Bắt tay

```
S -> {"type":"hello","app":"Redstone","protocol":1,"nonce":"<32 hex>"}
C -> {"cmd":"auth","proof":"<hex HMAC-SHA256(key=token UTF-8, msg=nonce UTF-8)>"}
S -> {"type":"auth","ok":true}                       (hoặc ok:false + error rồi đóng kết nối)
```

## Yêu cầu / phản hồi

```
C -> {"id":7,"cmd":"status","server":"Survival"}
S -> {"id":7,"ok":true,"data":{...}}
S -> {"id":7,"ok":false,"error":"..."}
```

`server` là id hoặc tên (không phân biệt hoa/thường; trùng tên thì phải dùng id).

| cmd | tham số | `data` |
|---|---|---|
| `ping` | - | `{pong, protocol}` |
| `list` | - | `{servers:[{id,name,type,state,port,playersOnline}]}` |
| `status` | server | thông tin chi tiết + `players[]` |
| `start` | server | `{state}` (chạy đồng bộ, chỉ khi Stopped/Crashed) |
| `stop` / `restart` | server | `{accepted:true}` (chạy nền; hỏi lại bằng `status`) |
| `console` | server, `lines` (1-2000) | `{lines[]}` |
| `send` | server, `command` | `{sent}` (một dòng, không ký tự điều khiển, ≤512) |
| `players` | server | `{online[], whitelist[], ops[], banned[], bannedIps[]}` |
| `player` | server, `action`, `name`, `reason`/`mode`/`text` | `{sent}` |
| `backups` | server | `{backups:[{name,sizeBytes}]}` |
| `backup` | server | `{name}` (trả lời khi nén xong; dùng save-off/save-all flush) |
| `props` | server | `{properties:{}}` (`rcon.password` bị che) |
| `prop-set` | server, `key`, `value` | `{key, restartRequired}` |
| `subscribe` / `unsubscribe` | tuỳ chọn server | `{subscribed}` |

`player.action`: `op`, `deop`, `kick`, `ban`, `pardon`, `ban-ip`, `pardon-ip`, `whitelist-add`,
`whitelist-remove`, `whitelist-on`, `whitelist-off`, `whitelist-reload`, `gamemode`, `msg`, `say`.
Tên người chơi phải khớp `^[A-Za-z0-9_.]{1,32}$`; lý do/nội dung không được chứa ký tự điều khiển
(chặn chèn thêm lệnh qua xuống dòng).

## Sự kiện (sau `subscribe`)

```
{"type":"event","event":"console","server":"<id>","line":"..."}
{"type":"event","event":"state","server":"<id>","state":"Running"}
```

## Cố ý không có

Thêm/xoá server, khôi phục backup, sửa file tuỳ ý, cài mod/plugin, đổi cài đặt app, tắt app.
