# redstonectl

CLI (Go, chỉ dùng thư viện chuẩn) để điều khiển từ xa Redstone. Chỉ hỗ trợ những thao tác có trong app.
Giao thức: [`docs/REMOTE_PROTOCOL.md`](../../docs/REMOTE_PROTOCOL.md).

## Bật phía app

*Cài đặt > Điều khiển từ xa* → bật, bấm **Áp dụng**, copy **token**. App chỉ lắng nghe `127.0.0.1:25585`.

## Build

```
cd cli/redstonectl
go build -o redstonectl .                              # máy hiện tại
GOOS=windows GOARCH=amd64 go build -o dist/redstonectl-windows-amd64.exe .
```
`build.sh` / `build.ps1` build đủ các nền tảng vào `dist/`. Test: `go test ./...`.

## Tài khoản & phân quyền

Mỗi token trong app là một **tài khoản** có vai trò `viewer` (chỉ xem), `operator` (+ Start/Stop/Restart, người chơi, backup)
hoặc `admin` (+ lệnh console thô, sửa server.properties, nhật ký), và có thể bị giới hạn trong một số server.
Chọn tài khoản bằng `--user <tên>` (mặc định `admin`, hoặc `REDSTONE_USER`, hoặc trong profile).
App kiểm tra quyền ở **mỗi lệnh** — CLI chỉ hiển thị. Xem `redstonectl whoami`, `redstonectl perms`, `redstonectl audit` (admin).
Bị từ chối quyền thì CLI thoát với mã 3.

## Chi tiết & chẩn đoán

- `redstonectl info` — một màn hình: CLI (phiên bản, hệ điều hành), OpenSSH Client (đường dẫn, phiên bản), cấu hình/profile,
  kết nối (đích, độ trễ), tài khoản và quyền, app (phiên bản, thời gian chạy, số server/người chơi), bảng server. `--json` để xuất JSON.
- `redstonectl doctor` — chẩn đoán từng bước: cấu hình, token, OpenSSH, đường hầm SSH, TCP, xác thực, app, server; mỗi bước lỗi có gợi ý cách sửa.
- Logo redstone vẽ bằng **màu nền** (khối pixel, truecolor hoặc 256 màu); không có màu thì dùng ASCII.

## Dùng

```
redstonectl --token <token> list                          # cùng máy
redstonectl --ssh bạn@may-chu --token <token> list        # qua OpenSSH (cần `ssh` trong PATH)
redstonectl profile add home --ssh bạn@may-chu --user ops --token <token> --default
redstonectl list
redstonectl status Survival
redstonectl restart Survival --wait
redstonectl console Survival -n 50 -f                     # theo dõi log trực tiếp
redstonectl send Survival "say xin chào"
redstonectl ban Survival Griefer phá nhà
redstonectl whitelist Survival add Alex
redstonectl backup Survival
redstonectl prop-set Survival motd Chào mừng
redstonectl shell Survival                                # phiên tương tác, bỏ qua tham số <server>
```

Cấu hình kết nối theo thứ tự ưu tiên: cờ dòng lệnh > biến môi trường (`REDSTONE_TOKEN`, `REDSTONE_SSH`, ...) >
profile (`%AppData%\redstonectl\config.json` hoặc `~/.config/redstonectl/config.json`, quyền 0600). `redstonectl help` liệt kê hết.

Thêm: logo ASCII (`redstonectl banner`), màu (tắt: `--no-color`/`NO_COLOR`), `--lang vi|en`, `--yes`.
Nếu máy chưa có OpenSSH Client, `--ssh` sẽ hỏi và cài giúp (Windows: `Add-WindowsCapability` qua UAC; Linux: apt/dnf/yum/pacman/zypper/apk).

Mã thoát: 0 thành công, 1 lỗi, 2 sai cú pháp, 3 không đủ quyền.
