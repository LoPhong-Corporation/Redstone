# Installer (Inno Setup)

`MinecraftServerManager.iss` đóng gói toàn bộ app (exe + Qt DLL/plugin do `windeployqt` chuẩn bị
sẵn) thành **một file cài đặt duy nhất**, gửi cho ai cũng cài được mà không cần cài Qt riêng.

## Build installer

1. Cài **Inno Setup 6** (miễn phí): https://jrsoftware.org/isinfo.php
2. Build app ở chế độ Release (CMake tự chạy `windeployqt` sau build):
   ```powershell
   cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
   cmake --build build --config Release
   ```
3. Biên dịch installer — một trong hai cách:
   - Mở `installer\MinecraftServerManager.iss` bằng Inno Setup Compiler → **Compile (F9)**, hoặc
   - Dòng lệnh: `"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\MinecraftServerManager.iss`
4. Kết quả: `installer\Output\MinecraftServerManager-Setup-1.0.0.exe`

Nếu thư mục build của bạn không phải `build\Release`, sửa dòng `#define MyBuildDir` ở đầu file `.iss`.

## Installer làm gì

- Cài **theo từng người dùng** vào `%LocalAppData%\Programs\Minecraft Server Manager` — **không cần
  quyền Admin, không hiện UAC**. Lý do: app lưu `data\servers.json` và `logs\` ngay cạnh file
  `.exe`; cài vào `C:\Program Files` (thư mục bị khoá ghi với user thường) sẽ làm việc lưu danh
  sách server hỏng âm thầm.
- Shortcut trong Start Menu + mục gỡ cài đặt; tuỳ chọn icon Desktop.
- Tuỳ chọn **"Khởi động cùng Windows (Silent)"** — ghi đúng Registry Run key mà checkbox trong app
  (`🚀 Cài đặt ứng dụng`) đang quản lý, nên bật ở đây thì trong app cũng thấy đã bật, tắt trong app
  vẫn hoạt động, và app không bao giờ chạy 2 lần mỗi lần đăng nhập. Tự xoá khi gỡ cài đặt.
- Gỡ cài đặt **không xoá** `data\` và `logs\` (chỉ chứa danh sách profile server, không chứa world
  Minecraft — world nằm ở thư mục bạn chọn khi thêm server).

## Trước khi phát hành thật

- **Tạo GUID riêng** cho `AppId` (Tools → Generate GUID trong Inno Setup Compiler). GUID hiện tại
  là giá trị mẫu; `AppId` ổn định là thứ giúp bản cài mới nâng cấp đúng bản cũ thay vì cài song song.
- **Thêm icon**: đặt `app.ico` vào thư mục này rồi bỏ dấu `;` ở dòng `SetupIconFile=app.ico`.
- **Thêm giấy phép**: đặt `LICENSE.txt` ở thư mục gốc project rồi bỏ dấu `;` ở dòng `LicenseFile=`.
- **Visual C++ Runtime**: bản build MSVC cần *VC++ Redistributable x64* trên máy đích. Hầu hết
  Windows 10/11 đã có sẵn, nhưng không đảm bảo trên máy mới cài. Nếu gặp lỗi thiếu
  `VCRUNTIME140.dll`/`MSVCP140.dll`, cài bản redistributable từ Microsoft
  (https://aka.ms/vs/17/release/vc_redist.x64.exe) — installer này chưa nhúng sẵn nó.
- Tính năng **🔓 Mở port trên Firewall** vẫn cần chạy app bằng *Run as administrator* (sửa rule
  Windows Firewall luôn cần nâng quyền, bất kể cài ở đâu).
