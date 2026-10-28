# Minecraft Server Manager (Qt6 UI + Native Win32 Process Backend)

Triển khai theo bản đặc tả kỹ thuật (`Minecraft_Server_Manager___Technical_Specification.md`).
UI dùng **Qt6 Widgets** theo yêu cầu; **lớp quản lý tiến trình Minecraft vẫn giữ nguyên Win32 API
gốc** (CreateProcessW, Job Object, pipe...) như spec yêu cầu — Qt chỉ thay thế phần hiển thị.

Tiến độ: **Phase 1-6 đều đã có** (Phase 4 giờ có cả Restore backup, an toàn — di chuyển world cũ
thay vì xoá), cộng thêm tính năng lấy cảm hứng từ [Fork](https://github.com/ForkGG/Fork) (Import
Server có sẵn, xem/xoá plugin-mod đã cài, tự restart định kỳ có cảnh báo trong game), và mới nhất:
**tự khởi động cùng Windows** (chế độ Normal/Silent + tray icon), **tự Start server khi mở app**,
cùng vài **cải thiện hiệu năng**. Chi tiết từng phần bên dưới. Chưa làm: firewall rule tự động,
Discord bot, MSPT thật.

## Giao diện Fluent & cài đặt mới

- **Giao diện Fluent 2 / Windows 11**: lớp nền–thẻ–control, bo góc 4/8px, nút accent, ô nhập có viền focus dưới, tab gạch chân, scrollbar mảnh, icon Segoe Fluent Icons, toggle switch. Có chủ đề Sáng/Tối/Theo hệ thống và 6 màu nhấn. Trên Windows 11, thanh tiêu đề tự đổi sáng/tối, bo góc và Mica (qua DWM). Mã nguồn: `src/ui/FluentTheme.*`, `FluentWidgets.*`.
- **Cài đặt ứng dụng** (`AppSettingsDialog`): chủ đề, màu nhấn, cỡ chữ; console (font, cỡ, xuống dòng, tô màu theo mức log, timestamp, tự cuộn, số dòng); hệ thống (khởi động cùng Windows, nút X thu xuống khay, thông báo khay, xác nhận Stop/Restart). Lưu ở `data/app_settings.ini`.
- **Cài đặt server** (`ServerOptionsDialog`, mục mới): tên, port, RAM, Java, jar, preset JVM flags (Aikar/ZGC/Serial), tham số JVM/server bổ sung, tự restart khi crash, thời gian chờ Stop, **backup tự động định kỳ** + số bản giữ lại.
- **Gameplay & mạng** (`PropertiesDialog`): ~40 khoá `server.properties` chia 5 tab (Chung, Gameplay, Người chơi, Mạng & hiệu năng, Resource pack); chỉ ghi khoá đã có hoặc khác mặc định.
- **Người chơi** (`PlayersDialog`): danh sách online (theo dõi từ log join/left), whitelist, operators, bị cấm (người + IP) đọc từ file JSON, thao tác op/kick/ban/gamemode/tin nhắn, bật/tắt whitelist, thông báo `say`, gamemode mặc định, độ khó, AFK timeout, ngày/đêm/thời tiết. Xem được cả khi server đang tắt.
- Build cần Qt >= 6.5; đặt `-DCMAKE_PREFIX_PATH=...` hoặc biến môi trường `QT_DIR`.

## Đa ngôn ngữ, phóng giao diện, chi tiết thư viện, CLI có màu

- **Đa ngôn ngữ**: *Cài đặt > Giao diện > Ngôn ngữ* (Tiếng Việt / English / theo hệ thống). Chuỗi gốc trong code là tiếng Việt, bản dịch là từ điển JSON (`resources/i18n/en.json` nhúng vào exe). Thêm ngôn ngữ không cần build: đặt `<mã>.json` vào thư mục `translations/` cạnh exe — xem `resources/i18n/README.md`. Cần khởi động lại app để đổi ngôn ngữ (app hỏi và tự khởi động lại, đợi các server dừng an toàn trước).
- **Chữ dễ đọc / phóng cửa sổ**: cỡ chữ gốc tăng lên 10.5pt; *Cỡ chữ giao diện* 80-200% (phím tắt **Ctrl +**, **Ctrl -**, **Ctrl 0**); *Phóng to cả cửa sổ* (Tự động theo Windows hoặc 100-250%, qua `QT_SCALE_FACTOR`, cần restart); high-DPI không làm tròn nên 125%/150% của Windows vẫn nét; cửa sổ mở theo kích thước màn hình và nhớ vị trí/kích thước lần sau.
- **Nút đổi vị trí theo trạng thái**: hàng nút Start/Stop/Restart giờ thực sự đổi bố cục — Stopped/Crashed: chỉ **Start**; Running: **Stop** rồi **Restart**; đang Starting: chỉ **Stop**; đang chuyển trạng thái: một nút mờ. Ngoài ra khi rê chuột nút nhích lên 1px, khi bấm nhích xuống 1px.
- **Thư viện Mod/Plugin chi tiết hơn**: tab *Tổng quan* (tác giả và vai trò, thống kê, môi trường client/server, loader, khoảng phiên bản Minecraft và có hỗ trợ bản của server bạn không, giấy phép, ngày đăng/cập nhật, liên kết mã nguồn/báo lỗi/wiki/Discord/ủng hộ, thẻ), *Mô tả* (nội dung đầy đủ), *Phiên bản* (danh sách bản tương thích kèm changelog, chọn đúng bản muốn cài), *Ảnh* (thư viện ảnh); hiện tên các phụ thuộc bắt buộc.
- **Mod/Plugin đã cài**: kiểm tra và cập nhật theo SHA-512, giữ bản cũ trong `addons-backup/`. Cài modpack `.mrpack` thật (tải mod, chép overrides).
- **CLI `redstonectl`**: logo redstone ASCII (`redstonectl banner`, tự hiện ở `help`/`shell`), màu cho bảng/trạng thái/log/JSON/lỗi/cảnh báo (tắt bằng `--no-color` hoặc `NO_COLOR`), ngôn ngữ `--lang vi|en` (hoặc `REDSTONE_LANG`, mặc định theo hệ thống), và **tự hỏi cài OpenSSH Client** khi thiếu `ssh` (`--yes` để tự đồng ý). Trong app, trang *Điều khiển từ xa* cũng hiện trạng thái OpenSSH Server và hỏi cài giúp trên Windows.

## Icon, tên cửa sổ và điều khiển từ xa

- **Icon Redstone** (`resources/redstone.ico`, 16-256px, nền trong suốt, làm từ hình bạn gửi) được nhúng vào file exe (`resources/app.rc`), cửa sổ, khay hệ thống và installer. Tiêu đề cửa sổ: "Redstone - Minecraft Server Manager".
- **Điều khiển từ xa**: *Cài đặt > Điều khiển từ xa* bật một endpoint chỉ lắng nghe `127.0.0.1` (mặc định tắt, xác thực HMAC bằng token). CLI Go `cli/redstonectl` dùng nó, từ xa thì đi qua đường hầm OpenSSH. Chỉ có các thao tác của app: xem/Start/Stop/Restart, console (kể cả theo dõi trực tiếp), người chơi, backup, server.properties. Xem `cli/redstonectl/README.md` và `docs/REMOTE_PROTOCOL.md`.

## Lọc output console

Dưới thanh trạng thái của console có ô **Lọc** (tìm chữ, không phân biệt hoa/thường, tự áp dụng sau 0,2 giây) và danh sách loại: Tất cả, Cảnh báo + lỗi, Chỉ lỗi, Chat & người chơi (join/left/lệnh/chat), Lệnh & manager. Bộ đếm "hiển thị / tổng" hiện khi đang lọc. Log đầy đủ vẫn nằm trong bộ nhớ của server nên đổi hoặc xoá bộ lọc là thấy lại toàn bộ; dòng mới đến cũng được lọc theo cùng điều kiện.

## Thư viện Mod/Plugin (Modrinth)

- **Nhận biết server**: tự dò phiên bản Minecraft (cấu hình, tên jar hoặc `logs/latest.log`) và loader/nền tảng (Fabric, Quilt, Forge, NeoForge, Paper, Purpur, Spigot, Folia, Bukkit) để lọc kết quả và chọn đúng file; mặc định chọn Plugin cho Paper/Spigot... và Mod cho Fabric/Forge.
- **Bộ lọc & sắp xếp**: loại (mod, plugin, datapack, resource pack, shader, modpack), lọc tương thích, ẩn mod chỉ chạy client, sắp xếp (liên quan, tải nhiều, theo dõi, mới, cập nhật), tải thêm kết quả.
- **Chi tiết trước khi cài**: icon, mô tả, thẻ, lượt tải; hiện chính xác phiên bản/file sẽ cài (ưu tiên bản release), cảnh báo beta/alpha hoặc đã có file.
- **Phụ thuộc bắt buộc** (ví dụ Fabric API) được tìm và cài kèm, có hộp xác nhận liệt kê file và dung lượng.
- **Tải an toàn**: stream ra file `.part` có thanh tiến trình, kiểm tra SHA-512 do Modrinth công bố rồi mới đổi tên; chỉ nhận URL https thuộc `modrinth.com`; tên file bị chuẩn hoá để không ghi ra ngoài thư mục đích. Datapack vào `<world>/datapacks`.
- Modpack (.mrpack) chỉ được tải vào `modpacks/`, chưa tự giải nén/cài từng mod.
- Phiên bản Minecraft được lưu khi tạo server từ provider và sửa được ở **Cài đặt server**.

## Nâng cấp gần nhất

- **Sửa build**: thêm `cmake/CompilerOptions.cmake` (trước đó thiếu nên CMake báo lỗi ngay bước configure); bỏ đường dẫn Qt gán cứng — dùng `-DCMAKE_PREFIX_PATH=...` hoặc biến môi trường `QT_DIR`.
- **Sửa race khi Start**: trạng thái `Running` giờ được đặt *trước* khi thread theo dõi chạy, nên server chết ngay lúc khởi động (thiếu EULA, sai jar) hiện đúng `Crashed` thay vì bị ghi đè thành `Running`.
- **Stop khi đang Starting** giờ hoạt động (trước đó trả về false, không dừng được).
- **Backup an toàn khi server đang chạy**: tự gửi `save-off` + `save-all flush`, chờ xác nhận, nén, rồi `save-on` (`MinecraftServer::PrepareForBackup/FinishBackup`).
- **Restore an toàn hơn**: chặn zip-slip (đường dẫn `..`/tuyệt đối trong zip) trước khi động vào world; giải nén báo lỗi khi file zip bị cắt/đĩa đầy thay vì ghi dữ liệu rác.
- **Hiệu năng console**: `ConsoleBuffer` dùng `std::deque` (xoá dòng cũ O(1) thay vì dịch 5000 chuỗi mỗi dòng).

## Tính năng theo phase

### Phase 1 — Core MVP
- `CreateProcessW` + pipe redirect + Windows Job Object (`KILL_ON_JOB_CLOSE`).
- Console real-time trên `std::jthread` riêng, không block UI; gửi lệnh qua stdin.
- State machine đầy đủ + phát hiện crash + auto-restart (tối đa 5 lần/10 phút).
- Lưu server profile vào `data/servers.json` (JSON reader/writer tự viết, không phụ thuộc ngoài).
- RAII cho mọi `HANDLE`.

### Phase 2 — Process Monitoring
- `monitor::ProcessMonitor` lấy mẫu CPU%/RAM mỗi 2 giây (`GetProcessTimes` +
  `K32GetProcessMemoryInfo`, thuần Win32).
- **TPS**: tự động gửi `/tps` mỗi ~10s **chỉ với** server khai Paper/Spigot/Purpur/Bukkit/Folia
  (chọn ở "Loại server" khi tạo), parse `"TPS from last 1m, 5m, 15m: ..."` (tự bỏ mã màu §).
  Vanilla/Forge/Fabric không bị gửi lệnh lạ vào console. Gõ tay `/tps` cũng được nhận diện.
  **MSPT chưa làm** (định dạng phản hồi `/mspt` khác, chưa có mẫu thật để parse chắc).

### Phase 3 — Java Manager, cấu hình server, EULA, người chơi
- **Java Manager** (`javamanager::DetectInstallations`): quét `JAVA_HOME`, mọi thư mục trong
  `PATH`, và các thư mục cài đặt phổ biến (Oracle, Eclipse Adoptium/Temurin, Azul Zulu, Amazon
  Corretto, Microsoft Build of OpenJDK, BellSoft Liberica) để liệt kê sẵn trong dialog "Thêm
  Server" — không cần gõ tay đường dẫn `java.exe` nữa (vẫn có nút Browse cho trường hợp khác).
  *Không đọc Registry* (mỗi hãng có schema khác nhau) và *không chạy `java -version`* để lấy số
  phiên bản (tránh phải spawn nhiều tiến trình chỉ để liệt kê) — để dành cho bản sau nếu cần.
- **server.properties editor** (`⚙️ server.properties` trên toolbar): form quen thuộc cho các
  mục hay chỉnh nhất (MOTD, max-players, difficulty, gamemode, pvp, online-mode, whitelist,
  view-distance, spawn-protection, level-name, level-seed). Các dòng khác trong file được giữ
  nguyên khi lưu.
- **EULA helper** (`📜 Chấp nhận EULA`): khi server dừng ngay vì `eula.txt` có `eula=false`
  (hành vi mặc định của chính Minecraft ở lần chạy đầu), app tự nhận diện và **hỏi rõ** bạn có
  đọc & đồng ý với https://aka.ms/MinecraftEULA không — chỉ khi bấm Đồng ý mới ghi `eula=true`.
  Không có chuyện tự động bypass âm thầm.
- **Whitelist/OP UI** (`👥 Người chơi`, cần server đang chạy): gửi thẳng lệnh console
  `op`/`deop`/`whitelist add`/`whitelist remove`/`kick`/`ban`/`pardon` — để chính Minecraft tự lo
  việc tra UUID, thay vì tự sửa tay `ops.json`/`whitelist.json`.

### Phase 4 — Backup Manager
- `backup::CreateBackup`: zip world (`world`, `world_nether`, `world_the_end` — cái nào có) +
  `server.properties` thành `backups/backup-YYYY-MM-DD_HH-MM-SS.zip`. Zip tự viết (định dạng
  STORE, không nén — xem lý do trong `backup/ZipWriter.hpp`), stream từng file 64 KB một lần,
  không load cả file vào RAM nên an toàn với world nhiều GB.
- **Retention**: tự giữ lại `kMaxBackupsToKeep` = 10 bản mới nhất, bản cũ hơn tự xoá.
- Dialog `💾 Backups`: tạo (chạy nền, không đứng UI), xoá, và "📂 Mở thư mục backups".
- **Restore** (`⭯` trong dialog Backups): giải nén ngược lại vào thư mục server. Yêu cầu server
  đang **Stopped** (kiểm tra thật với trạng thái sống, không phải chỉ đọc config). An toàn theo
  thiết kế: world hiện tại (nếu có) được **di chuyển** (không xoá) sang
  `backups/pre-restore-<thời điểm>/` trước khi ghi đè gì cả — chọn nhầm bản backup hay lỡ tay vẫn
  lấy lại được. Tự viết `backup::ZipReader` đọc lại đúng định dạng của `ZipWriter` (chỉ đảm bảo
  đọc được zip do chính app này tạo, không phải unzip tổng quát).

### Phase 5 — Server Providers
- Trong dialog "Thêm Server", mục **"Tải server tự động"**: chọn Vanilla/Paper/Fabric → chọn
  phiên bản → bấm "⬇ Tải" — app tự tải thẳng file server.jar vào Thư mục server đã điền, và tự
  điền vào ô "Tên file jar". Không cần tự đi tìm/tải file jar ở đâu nữa.
  - **Vanilla**: `piston-meta.mojang.com/mc/game/version_manifest_v2.json` (chỉ liệt kê bản
    `release`, không hiện snapshot) → `downloads.server.url` của từng phiên bản.
  - **Paper**: ~~`api.papermc.io/v2/projects/paper`~~ → PaperMC đã khai tử API v2 (trả về HTTP
    410 Gone từ khoảng cuối 2025), chuyển sang API mới **"Fill"** tại `fill.papermc.io/v3`. Đã
    cập nhật: `/v3/projects/paper` (version group), `/v3/projects/paper/versions/{v}/builds`
    (mảng JSON phẳng, lọc `channel == "STABLE"`, lấy `downloads."server:default".url`).
  - **Fabric**: `meta.fabricmc.net/v2/versions/game` (chỉ bản `stable`) → lấy loader mới nhất
    tương thích (`/versions/loader/{version}`) + installer mới nhất (`/versions/installer`) → tải
    thẳng file server launcher tự-bootstrap ở endpoint `.../server/jar` (file này tự tải thêm
    vanilla+loader trong lần chạy đầu — vẫn cần internet ở lần Start đầu tiên).
  - **Forge/NeoForge không có** trong danh sách: 2 loader này phân phối dưới dạng file cài đặt
    tương tác (installer chạy wizard/CLI để vá vào jar vanilla), không phải 1 URL tải thẳng như
    Vanilla/Paper/Fabric — không khớp với mô hình "1 URL, 1 file" ở đây. Muốn dùng Forge, bạn vẫn
    tự chạy trình cài đặt Forge trước như bình thường, rồi trỏ "Tên file jar" vào file nó tạo ra.
  - Đã tra cứu tài liệu chính thức của cả 3 API trước khi viết code (không đoán field/endpoint),
    nhưng **chưa gọi thử request thật** — xem mục Giới hạn.

### Phase 6 — Network Tunnel (Playit.gg / Cloudflare Tunnel)
App **không** tự implement lại giao thức tunnel riêng của Playit.gg hay Cloudflare (không khả
thi/không phù hợp — đây là dịch vụ độc quyền). Thay vào đó, `⚙ Cài đặt nâng cao` cho mỗi server
cho phép:
- Trỏ tới file thực thi bạn đã tải sẵn (`playit.exe`, `cloudflared.exe`, hoặc bất kỳ exe nào) +
  tham số dòng lệnh.
- App quản lý vòng đời tiến trình đó y hệt cách quản lý `java.exe` (dùng lại `process::Process` —
  tức cũng được bọc trong Windows Job Object, không thể sống sót nếu Manager crash).
- Output của tunnel (bao gồm địa chỉ public nó in ra) được gộp thẳng vào console của server, đánh
  dấu tiền tố `[tunnel]` — không cần mở thêm cửa sổ terminal riêng.
- Tuỳ chọn tự khởi động tunnel cùng lúc Start server; tunnel cũng tự dừng khi server dừng.
- Nút Start/Stop tunnel thủ công ngay trong dialog, độc lập với server (test tunnel mà không cần
  chạy Minecraft).

### Windows Firewall + Java version thật + trình sửa file cấu hình chung
- **🔓 Mở port trên Firewall** (trong `🌐 Tunnel & Nâng cao`): chạy `netsh advfirewall firewall add
  rule` (qua chính `process::Process` sẵn có, không cần COM/`netfw.h`) để mở port TCP của server
  ra ngoài mạng LAN. Đây là hành động **thủ công, bấm 1 lần** (không tự động mỗi lần Start, tránh
  `netsh` tạo rule trùng lặp liên tục). **Cần app đang chạy với quyền Administrator** — nếu không
  sẽ báo lỗi rõ ràng thay vì âm thầm thất bại.
- **Java Manager giờ hiện số phiên bản thật**: sau khi liệt kê các Java tự phát hiện, app chạy nền
  `java -version` cho từng cái (không chặn UI) rồi tự thêm `(Java 17)`, `(Java 8)`... vào tên hiển
  thị trong combo — biết ngay bản nào hợp với phiên bản Minecraft mình định chạy mà không cần tự
  mở CMD kiểm tra.
- **📝 Sửa file cấu hình** (toolbar): mở bất kỳ file text nào trong thư mục server (không chỉ
  `server.properties`) — `bukkit.yml`, `spigot.yml`, config của từng plugin... — bằng trình soạn
  thảo văn bản thuần (monospace, chưa tô màu cú pháp) ngay trong app, không cần mở Notepad rời.

### Tự khởi động cùng Windows + chế độ Normal/Silent + hiệu năng
- **🚀 Cài đặt ứng dụng** (toolbar): bật "Khởi động cùng Windows" — ghi vào Registry Run key của
  user hiện tại (`HKCU\...\Run`, không cần quyền admin), kèm chọn chạy **Normal** (mở cửa sổ như
  bình thường) hay **Silent** (chạy ẩn, chỉ còn icon khay hệ thống — thêm cờ `--silent` vào lệnh
  khởi động).
- Mỗi server có checkbox riêng **"Tự động Start server này khi mở app"** (trong `🌐 Tunnel & Nâng
  cao`) — kết hợp với "Khởi động cùng Windows" là thành "server tự chạy ngay khi bật máy", không
  cần đăng nhập rồi tự tay mở app + bấm Start.
- **Tray icon**: đóng cửa sổ (nút X) giờ **ẩn xuống khay** thay vì thoát hẳn — server đang chạy
  vẫn tiếp tục chạy. Nhấp đúp hoặc chọn "Hiện cửa sổ" trong menu chuột phải để mở lại; "Thoát"
  trong menu đó mới thực sự đóng ứng dụng (và dừng các server đang chạy).
- **Hiệu năng**: trước đây mỗi lần BẤT KỲ server nào đổi trạng thái (Starting/Running/Stopping...)
  đều xoá sạch và dựng lại toàn bộ danh sách server trong UI. Giờ chỉ cập nhật đúng 1 dòng bị ảnh
  hưởng (`MainWindow::UpdateServerListItem`, tra theo `QMap<serverId, QListWidgetItem*>`), và chỉ
  tính lại trạng thái nút bấm (Start/Stop/Restart...) khi đúng server đang thay đổi là server đang
  được chọn — rõ rệt hơn khi quản lý nhiều server cùng lúc vì tần suất đổi trạng thái tăng theo số
  server. Danh sách chỉ rebuild toàn bộ khi có thay đổi cấu trúc thật sự (thêm/xoá/import server).

### Từ repo Fork (ForkGG/Fork) — đã thêm
- **📥 Import Server**: trỏ vào thư mục server có sẵn (đã có file .jar từ trước, có thể do tự
  tải hoặc chuyển từ tool khác) — `server::ScanServerDirectory` tự tìm file .jar, đoán loại
  server từ tên file, và đọc `server-port` từ `server.properties` nếu đã có, để không phải tự
  gõ tay từng thứ.
- **🧩 Đã cài**: liệt kê file `.jar` trong `plugins/` và `mods/` của server, xoá được — bổ sung
  cho Thư viện Modrinth (vốn chỉ cài, không xem/xoá được cái đã có).
- **Tự động Restart định kỳ**: đặt chu kỳ (giờ) trong `⚙ Cài đặt nâng cao`; app gửi cảnh báo
  `say [Manager] Server sẽ tự khởi động lại sau 60 giây.` vào chat trước khi restart, không restart
  đột ngột giữa lúc người chơi đang chơi.
- Discord bot điều khiển server (có trong Fork) — **chưa làm**, xem mục Giới hạn.

### UI chung
- Theme tối (QSS + style Fusion), nút bấm phân màu theo hành động.
- Toolbar: Server mới / Xoá server / server.properties / Người chơi / Backups / Chấp nhận EULA /
  Thư viện Mod-Plugin / Làm mới.
- Trang khởi động (empty state) khi chưa có server nào.
- Nút Browse cho Thư mục server / file jar / java.exe trong dialog "Thêm Server".
- **Thư viện Modrinth** (`net::ModrinthClient` + `ui::LibraryDialog`, dùng
  `QNetworkAccessManager`/`QJsonDocument` sẵn có trong Qt): tìm & cài Mod/Plugin/Resource
  Pack/Shader vào đúng thư mục (`mods/`, `plugins/`, `resourcepacks/`, `shaderpacks/`) của server
  đang chọn. Modpack chỉ tải `.mrpack` về, chưa tự cài. Cần internet khi dùng; mọi phần khác của
  app chạy offline hoàn toàn.

## Đóng gói installer

Thư mục `installer/` có script **Inno Setup** (`MinecraftServerManager.iss`) đóng gói app thành 1
file cài đặt duy nhất (cài theo từng user, không cần Admin, có Start Menu/Desktop shortcut, tuỳ
chọn khởi động cùng Windows, gỡ cài đặt). Xem `installer/README.md` để biết cách build và các việc
cần làm trước khi phát hành (GUID riêng, icon, giấy phép, VC++ Runtime).

## Toolbar

Toolbar được gom nhóm để không bị tràn trên màn hình nhỏ: **Server ▾** (Server mới / Import / Xoá),
**⚙️ Quản lý ▾** (server.properties / Người chơi / Backups / Sửa file cấu hình / EULA),
**🧩 Mods ▾** (Thư viện / Đã cài), cùng 3 nút riêng: Tunnel & Nâng cao, Cài đặt ứng dụng, Làm mới.

## Yêu cầu

- **Visual Studio 2022+ (MSVC)**, **CMake ≥ 3.21**, Windows 10/11 x64.
- **Qt6** (khuyến nghị 6.5+), component `Core` + `Widgets` + `Network`. Cài qua Qt Online
  Installer: https://www.qt.io/download-qt-installer — chọn bản ứng với trình biên dịch của bạn,
  ví dụ `MSVC 2019 64-bit` hoặc `MSVC 2022 64-bit`.

## Build

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"
cmake --build build --config Release
```

Thay đường dẫn Qt cho đúng máy bạn (thư mục chứa `lib/cmake/Qt6`). Sau khi build, CMake tự chạy
`windeployqt` để copy DLL Qt cần thiết vào cạnh file `.exe`.

Chạy smoke test (không cần Qt/GUI, chỉ test JSON + ConfigManager):

```powershell
cmake -S . -B build -A x64 -DMSM_BUILD_TESTS=ON
cmake --build build --config Release --target MinecraftServerManagerTests
ctest --test-dir build -C Release --output-on-failure
```

## ⚠️ Quan trọng: chưa build/test được trong môi trường tạo ra code này

Code được viết trong môi trường **Linux, không có MSVC/Windows SDK, không có Qt6**, nên **chưa
compile thử được lần nào**. Mình đã soát kỹ từng file theo tay (include, chữ ký hàm Qt, thứ tự
hủy đối tượng, an toàn đa luồng...), nhưng vẫn có khả năng sót lỗi nhỏ mà chỉ MSVC + Qt thật mới
bắt được. Build theo hướng dẫn trên rồi gửi lại **nguyên văn lỗi** (toàn bộ Error List, không chỉ
vài dòng đầu) để mình vá đúng chỗ.

## Sử dụng

1. Chạy `MinecraftServerManager.exe`.
2. Toolbar → **"🆕 Server mới"** (hoặc nút "+ Tạo Server Đầu Tiên" ở màn hình chào) → điền Tên,
   Thư mục, file jar, chọn Java (combo tự phát hiện hoặc Browse), Loại server, RAM, Port → Save.
3. Chọn server, bấm **▶ Start**.
4. Xem console real-time, gõ lệnh + Gửi (hoặc Enter). CPU/RAM/TPS hiện trên thanh trạng thái.
5. Cần chỉnh MOTD/difficulty/... → **⚙️ server.properties**. Quản lý người chơi khi server đang
   chạy → **👥 Người chơi**. Backup thủ công/tự động dọn cũ → **💾 Backups**. Cài mod/plugin →
   **📚 Thư viện Mod/Plugin**; xem/xoá cái đã cài → **🧩 Đã cài**.
6. Nếu server dừng ngay vì chưa chấp nhận EULA, app tự hỏi — đồng ý thì bấm Yes, hoặc dùng
   **📜 Chấp nhận EULA** trên toolbar bất cứ lúc nào.
7. Đã có sẵn 1 server ở đâu đó (không cần tạo mới) → **📥 Import Server**, trỏ vào thư mục đó.
8. Muốn public server qua Playit.gg/Cloudflare Tunnel, hoặc tự restart định kỳ → **🌐 Tunnel &
   Nâng cao** (mục này cũng có "Tự động Start server khi mở app").
9. Muốn máy tự chạy server ngay khi bật lên, không cần tự mở app → **🚀 Cài đặt ứng dụng**, bật
   "Khởi động cùng Windows" (chọn Silent nếu không muốn cửa sổ tự bật lên mỗi lần khởi động máy).
10. Đóng cửa sổ (nút X) giờ chỉ ẩn xuống khay hệ thống, server vẫn chạy — bấm phải vào icon khay
    → "Thoát" mới thực sự đóng app.

## Không nằm trong bản này

- **MSPT thật** — xem Phase 2.
- **Java Manager đọc Registry** — vẫn chỉ quét filesystem/PATH/JAVA_HOME (probe version thì đã
  làm, xem phần tính năng ở trên).
- **Cài modpack tự động** (giải nén `.mrpack`, tải dependency, áp overrides) — Thư viện Modrinth
  hiện chỉ tải file `.mrpack` về.
- **Forge/NeoForge auto-download** — xem Phase 5, lý do kỹ thuật (installer tương tác, không
  phải 1 URL tải thẳng).
- **Discord bot** (điều khiển server từ Discord, có trong repo Fork) — cần thêm thư viện
  WebSocket/Gateway client cho Discord + OAuth, đi ngược triết lý "tối thiểu dependency" của
  project này; để dành làm riêng nếu bạn thực sự cần, thay vì làm dở dang.
- **Syntax highlight cho trình sửa file** — `📝 Sửa file cấu hình` hiện là text thuần, chưa tô màu
  cú pháp YAML/properties.
- **Playit.gg/Cloudflare Tunnel**: chỉ quản lý vòng đời tiến trình bạn đã cài sẵn, không tự tải
  giúp `playit.exe`/`cloudflared.exe`, không tự tạo Cloudflare Tunnel/route DNS giúp bạn (cần tài
  khoản Cloudflare, tự làm trên Dashboard/CLI của họ trước).
- **Firewall rule tự động khi Start** — đã có nút mở port thủ công (xem phần tính năng ở trên),
  chưa tự động hoá theo mỗi lần Start (cố ý, tránh spam rule trùng — xem
  `platform/FirewallManager.hpp`).
- Modrinth "Plugin" filter dùng facet `project_type:plugin` — mình đã tra docs.modrinth.com
  nhưng chưa test request thật; nếu trả về rỗng, thử đổi qua "Mod" (nhiều plugin Bukkit/Paper
  vẫn được gắn category "mod" trên Modrinth).

## Giới hạn kỹ thuật đã biết (có chủ đích, không phải bug bỏ sót)

- **Restore đã làm nhưng có giới hạn**: `ZipReader` chỉ đảm bảo đọc đúng file zip do chính
  `ZipWriter` của app này tạo ra (giả định EOCD nằm ở đúng 22 byte cuối, không có comment) — không
  phải bộ giải nén tổng quát, đừng dùng để mở zip tải từ nơi khác. Nếu quá trình giải nén lỗi giữa
  chừng (hết dung lượng đĩa, mất điện...), world cũ vẫn an toàn trong thư mục
  `backups/pre-restore-<thời điểm>/` vì đã được di chuyển sang đó **trước** khi ghi đè — nhưng
  world mới có thể bị dở dang, cần restore lại lần nữa hoặc tự khôi phục thủ công từ thư mục đó.
- `ZipWriter`/`ZipReader` chỉ hỗ trợ STORE (không nén) và không hỗ trợ ZIP64 (không backup được 1
  file nào đó > 4 GB — bản thân từng file trong world Minecraft hiếm khi to vậy, region file
  thường vài MB).
- `core::EventDispatcher` chưa có `Unsubscribe()`. Mọi server đang chạy được `Stop()` trước khi
  `MainWindow` bị hủy khi thoát app bình thường nên không có thread nền nào gọi `Publish()` sau
  khi `MainWindow` đã bị hủy trong trường hợp thông thường; có một khoảng hẹp giữa lúc đóng cửa
  sổ và lúc dừng xong các server nơi 1 sự kiện trễ về lý thuyết có thể nhắm vào con trỏ đã
  dangling — vô hại vì tiến trình thoát ngay sau đó.
- ID server sinh từ thời gian hệ thống + counter, không dùng GUID/COM.
- JSON reader/writer tự viết cho `data/servers.json` chỉ đủ dùng cho đúng schema `ServerConfig`.
- Java Manager không đọc Windows Registry, không probe `java -version` (xem Phase 3 ở trên).

## Cấu trúc

```
src/
  app/            Application - khởi tạo QApplication, wiring, shutdown
  core/           Types, Event, EventDispatcher, Logger, Json, StringUtil (dùng chung)
  process/        Process, ProcessPipes - Win32 CreateProcessW + Job Object
  monitor/        ProcessMonitor - CPU%/RAM sampling (Phase 2)
  console/        ConsoleBuffer, ConsoleController - đọc pipe trên thread riêng
  javamanager/    JavaManager - dò Java cài sẵn (Phase 3)
  backup/         ZipWriter, BackupManager (Phase 4)
  platform/       StartupManager - bật/tắt khởi động cùng Windows (Registry Run key)
  server/         MinecraftServer (state machine + EULA + tunnel + scheduled restart),
                  ServerManager, ServerImporter (Fork-inspired import)
  config/         ConfigManager (data/servers.json), ServerProperties (server.properties)
  net/            ModrinthClient (mod/plugin/modpack), ServerProviderClient (Phase 5: tải
                  server.jar Vanilla/Paper/Fabric)
  ui/             MainWindow (+ tray icon, closeEvent ẩn xuống khay), AddServerDialog,
                  ImportServerDialog, PropertiesDialog, PlayersDialog, BackupsDialog,
                  InstalledAddonsDialog, AdvancedSettingsDialog (tunnel + scheduled restart +
                  auto-start-with-app), AppSettingsDialog (khởi động cùng Windows), LibraryDialog
```
