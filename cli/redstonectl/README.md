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

## Dùng

```
redstonectl --token <token> list                          # cùng máy
redstonectl --ssh bạn@may-chu --token <token> list        # qua OpenSSH (cần `ssh` trong PATH)
redstonectl profile add home --ssh bạn@may-chu --token <token> --default
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

Mã thoát: 0 thành công, 1 lỗi, 2 sai cú pháp.
