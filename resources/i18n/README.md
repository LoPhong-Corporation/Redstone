# Dịch / Translations

- Chuỗi gốc trong code là **tiếng Việt**; mỗi file `<mã>.json` là một từ điển `{ "chuỗi gốc": "bản dịch" }`.
- `en.json` (tiếng Anh) được nhúng vào exe qua `resources.qrc`. `vi.json` chỉ sửa vài thông báo gốc tiếng Anh của backend sang tiếng Việt.
- **Thêm ngôn ngữ không cần build lại**: chép `en.json` thành `<mã>.json` (vd `fr.json`), đổi `"_name"` thành tên ngôn ngữ, dịch các giá trị, rồi đặt vào thư mục `translations/` cạnh file exe. Ngôn ngữ mới tự xuất hiện trong *Cài đặt > Giao diện > Ngôn ngữ*.
- Giữ nguyên chỗ giữ chỗ `%1`, `%2`, `{}` và ký tự `\n`. Chuỗi nào thiếu thì hiện bản tiếng Việt gốc.
- `.keys_snapshot.json` là danh sách chuỗi gốc tại thời điểm tạo bản dịch (để so khi code đổi).
