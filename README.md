# OpenStation C245/C210 — firmware cho "Trạm hàn v4"

Firmware dựa trên [OpenStation 0.1.0 Build 14](https://github.com/imdolce/OpenStation-firmware) của imdolce, viết lại cho đúng hai tay hàn **JBC C245** và **JBC C210**, với giao diện ngang 160×128 mới. Phần cứng mạch của **toanthodien16**, giao diện lấy cảm hứng từ [AxxSolder](https://github.com/AxxAxx/AxxSolder).

Phiên bản: **1.6.1**.

## Giữ nguyên từ bản gốc

- Hiệu chuẩn và hệ số PID của C245 do tác giả đo trên mạch:
  - Thang đo 3500 µV/°C, nhiệt độ phòng mặc định 25 °C.
  - Kp 8.0, Ki 2.0, Kd 0.5. Ở nguồn 24 V, các khâu P, I, D cho ra đúng con số như bản gốc.
  - Điện áp định mức 24 V.
- Nhịp nung 20 Hz (xung tối đa 44 ms) và cách đo nhiệt trong khoảng nghỉ 5 ms.
- Bộ lọc ADC và toàn bộ bảo vệ: hở mũi, quá nhiệt, nguồn ngoài dải, dòng rò, mất ADC, quá thời gian xung, tụt tín hiệu.

## Bộ điều khiển nhiệt (từ 1.1.0)

Hệ số giữ nguyên, phần thuật toán quanh nó được làm lại:

1. **Bù điện áp nguồn.** PID tính theo công suất, lấy 24 V làm mốc, rồi đổi ra duty theo điện áp đo được (duty = u·24²/V²). Mũi phản ứng như nhau dù nguồn là 12, 19, 24 hay 28 V. Bản gốc ở 12 V chỉ cho 1/4 công suất mỗi % duty, nên rất ì.
2. **Bù trước (feedforward).** Máy cấp sẵn công suất giữ nhiệt, tỉ lệ với (nhiệt đặt − nhiệt phòng). Hệ số này tự học mỗi khi mũi ổn định ở nhiệt đặt, nên khâu I chỉ còn bù sai lệch nhỏ. Thử với giá trị khởi đầu sai gấp 3 lần: sau 45–60 giây hệ số tự hội tụ, nhiệt vẫn giữ ±1 °C suốt quá trình.
3. **Khâu I chỉ hoạt động trong vùng ±15 °C quanh nhiệt đặt.** Hết hiện tượng tích phân tràn lúc làm nóng, nguyên nhân gây vọt 10–20 °C ở bản gốc.
4. **Khâu D có lọc thông thấp** để bớt rung duty do nhiễu ADC.
5. **Chống tràn tích phân** dựa trên công suất thực sự cấp được, kể cả khi bị giới hạn bởi điện áp hoặc công suất.
6. **Bù nhiệt độ phòng (mối nối lạnh):** cài đặt **Room temp**, mặc định 25 °C như bản gốc. Ở Sài Gòn nên đặt khoảng 30 °C. Mạch không có cảm biến nhiệt độ phòng, nên mục này phải chỉnh tay. Bảng hiệu chuẩn 6 điểm nên làm sau khi đã đặt Room temp.

So sánh trên mô phỏng (mô hình nhiệt 2 khối, chạy `sim/pid_bench.sh`), C245, 25 → 350 °C:

| Nguồn | Vọt khi làm nóng (cũ → mới) | Vọt ở đầu mũi thật | Nhiệt đo tụt khi hàn mối 40 W |
| --- | --- | --- | --- |
| 12 V | 22 → 3 °C | 11,5 → 1,9 °C | 29 → 13 °C |
| 19 V | 16 → 3 °C | 15,2 → 2,7 °C | 18 → 13 °C |
| 24 V | 14 → 3 °C | 12,0 → 1,7 °C | 13 → 13 °C |
| 28 V | 10 → 3 °C | 8,5 → 1,9 °C | 11 → 13 °C |

Đổi lại, đầu mũi lên tới 340 °C chậm hơn khoảng 1,5 giây ở 24 V, vì không còn cho lõi nung vọt lố để đẩy nhiệt ra đầu mũi. Mô hình nhiệt chỉ gần đúng, kết quả trên mạch thật cần kiểm chứng bằng nhiệt kế và biểu đồ trên màn chính.

## Thao tác

| Thao tác | Màn chính | Trong cài đặt |
| --- | --- | --- |
| Xoay | Chỉnh nhiệt đặt (thoát Boost nếu đang Boost). Chỉ ở màn chính: ở màn Sleep xoay không làm gì, ở màn logo xoay chỉ đánh thức màn chính | Di chuyển / đổi giá trị |
| Bấm 1 lần | Sau khi xoay núm: lưu nhiệt đặt (đang tắt thì lưu và nung). Không đổi gì: bật / tắt nung | Chọn hoặc xác nhận |
| Bấm đúp | Đang nung: Boost (bấm đúp lần nữa để hủy) | — |
| Giữ 0,8 s | Tắt nung, mở cài đặt | Lưu và thoát |

## Màn hình

- **Bật nguồn:** logo, bên dưới là tay hàn đang chọn và tình trạng mũi (đã nhận / không có mũi / chưa hiệu chuẩn). Sau 3,5 giây tự vào màn chính, bấm 1 lần để bỏ qua. Tắt được trong cài đặt (**Startup logo**).
- **Khi tắt nung:** chỉ hiện logo. Lúc mũi còn trên 60 °C có thêm dòng "Tip hot". Xoay núm sẽ hiện lại màn chính trong 10 giây để chỉnh nhiệt. Bấm 1 lần để nung.
- **Màn chính:**
  - Hàng trên: tên tay hàn, thông báo, trạng thái (OFF / HEAT / READY / BOOST / SLEEP / FAULT).
  - Điện áp, dòng, công suất.
  - Số nhiệt mũi lớn và nhiệt đặt (màu đỏ khi Boost), dạng số chạy: chữ số nào đổi thì trượt lên (tăng) hoặc xuống (giảm), các chữ số khác đứng yên. Nhiệt mũi bỏ qua dao động ±1 °C ngắn hơn 0,4 s để số không nhảy liên tục.
  - Thanh công suất kèm %.
  - Biểu đồ lớn khoảng 80 giây: đường xanh dương là nhiệt mũi, xanh lá là % công suất, chấm xám là nhiệt đặt.
- **Màn Sleep** (gác tay hàn lên giá): nhiệt mũi hiện tại, nhiệt đang giữ, và thời gian đếm ngược đến lúc tắt nung.
- **Hết giờ Sleep:** máy tắt nung và hiện logo, đèn nền dịu còn 25%. Nhấc tay hàn lên thì sáng lại; bấm 1 lần để nung.

## Cài đặt (tự lưu khi thoát menu)

| Mục | Phạm vi | Mặc định |
| --- | --- | --- |
| Handle (tay hàn) | C245 / C210 | C245 |
| Boost temp | Off, +5 … +100 °C | +50 °C |
| Boost time (tính **từ lúc đạt** nhiệt Boost) | 10 … 120 s | 30 s |
| Sleep after (gác bao lâu thì ngủ) | Tức thì … 60 s | 5 s |
| Sleep temp | 100 … 250 °C | 150 °C |
| Off after sleep (ngủ bao lâu thì tắt nung) | 1 … 60 phút | 20 phút |
| Max temp | 150 … 500 °C | 450 °C |
| Room temp (bù nhiệt độ phòng) | 10 … 45 °C | 25 °C |
| Knob step, Knob direction, Brightness, Sound, Ready beep, Startup logo, Flip screen | | |
| Calibrate tip, Factory reset (bấm 2 lần), About, Save and exit | | |

**Nhớ nhiệt đặt:** xoay tới nhiệt muốn dùng rồi **bấm 1 lần** trên màn hình chính để lưu (chữ SET màu vàng = chưa lưu, xám = đã lưu). Đang nung thì lần bấm này chỉ lưu, máy vẫn nung; đang tắt thì lưu và bắt đầu nung. Không lưu trong lúc Boost. Mỗi tay hàn (C245, C210) nhớ mức riêng; bật lại máy sẽ về đúng mức đã lưu.

**Cách lưu flash:** cài đặt nằm ở 2 KiB cuối flash (0x0800F000–0x0800F7FF), ghi xoay vòng qua 8 khối 256 byte. Mỗi lần lưu ghi vào khối kế tiếp nên flash bền gấp 8 lần; mất điện đúng lúc đang ghi thì máy dùng bản lưu trước đó. Kiểm tra trên máy tính: `sim/settings_test.sh`.

Cài đặt chỉ được ghi vào flash khi có thay đổi. Lần đầu nạp firmware, nếu trong chip còn cài đặt của OpenStation Build 13/14, firmware sẽ tự chuyển sang các thông số Boost, Sleep, preset, độ sáng, âm thanh, và bảng hiệu chuẩn C245.

## C210: phải hiệu chuẩn trước khi dùng nhiệt cao

Bản gốc đặt C210 là 8200 µV/°C. Đó là giá trị tạm, sẽ làm máy đọc thấp hơn thực tế khoảng 6 lần và mũi quá nhiệt. Bản này đặt các giá trị khởi đầu an toàn hơn:
- Thang đo **1300 µV/°C**, quy từ tỉ lệ C210/C245 của AxxSolder.
- Trần **65 W**.
- Tối đa **300 °C** khi chưa hiệu chuẩn. Màn chính hiện chữ "UNCALIBRATED".

Cách hiệu chuẩn: vào **Calibrate tip**, cần một nhiệt kế đo mũi hàn.
1. Máy lần lượt nung tới 6 mức: 100, 200, 300, 350, 400, 450 °C.
2. Ở mỗi mức, xoay núm cho khớp với số trên nhiệt kế.
3. Chờ chữ "READY" rồi bấm.
4. Giữ núm bất cứ lúc nào để dừng.

Sau khi đủ 6 mức, máy lưu bảng hiệu chuẩn và bỏ giới hạn 300 °C.

## Build và nạp

- **PlatformIO:** `pio run`, sau đó `pio run -t upload` qua WCH-LinkE.
- **Không dùng PlatformIO scons:** `tools/build.sh` tạo `build/openstation.hex` và `build/openstation.bin`. Script dùng toolchain mà PlatformIO đã cài sẵn.
- **WCH-LinkUtility:** nạp file `.hex`, hoặc nạp file `.bin` tại địa chỉ `0x08000000`.

Code chiếm 60 KiB đầu flash. 2 KiB cuối (`0x0800F000`) dành cho cài đặt. Nếu chọn xóa toàn chip, cài đặt cũ sẽ mất.

**Mô phỏng trên máy tính:** chạy `sim/run.sh`. Lệnh này chạy code giao diện và điều khiển thật với mô hình nhiệt, kiểm tra Boost, Sleep, preset và lỗi, rồi xuất ảnh mọi màn hình ra `sim/out/`. Chạy `sim/pid_bench.sh` để so sánh PID cũ và mới ở 12/19/24/28 V.

**Logo:** mặc định là chữ "OpenStation". Muốn dùng logo riêng: chạy `python3 tools/gen_logo.py logo_cua_ban.png`, sau đó build lại. Ảnh nền trong suốt hoặc nền trắng đều được: nét đen/xám tự đổi thành trắng cho hợp màn nền đen, phần có màu giữ nguyên. Firmware lưu 2 cỡ (156×80 cho màn khởi động, 156×104 cho màn tắt nung), 32 màu. `python3 tools/gen_logo.py --default` để về chữ OpenStation.

**Tạo lại font:** `python3 tools/gen_fonts.py`. Font dùng là DejaVu Sans, giấy phép Bitstream Vera, được phép nhúng.

## Lưu ý an toàn

- Đây là firmware điều khiển mỏ hàn công suất lớn. Mỗi lần nạp bản mới, hãy thử với nhiệt kế ngoài trước khi dùng thật.
- Luôn chọn đúng tay hàn. Chọn sai sẽ làm nhiệt đọc sai và có thể làm quá nhiệt.
- Firmware chưa có ngắt quá dòng tức thời, giống bản gốc.

## Thay đổi so với bản gốc

- Chỉ giữ C245 và C210. Bỏ profile tùy chỉnh và 7 họ mũi không dùng, giải phóng khoảng 9 KB flash.
- Boost đếm giờ từ lúc đạt nhiệt. Bấm đúp lần nữa để hủy. Phần đếm giờ đã được chuyển ra khỏi ngắt 1 ms.
- Sleep có ba thông số chỉnh được, và có màn Sleep riêng.
- Sửa lỗi cờ "đang nung" không được xóa sau khi có lỗi, từng gây báo TIP OPEN giả.
- Bỏ "Ignore Faults" khỏi menu và không lưu nó vào flash nữa.
- Bỏ phần bù +300 °C còn sót lại. Đó là chỗ vá cho một lỗi tràn số đã được sửa.
- Bỏ phép tính số thực khi đổi điện áp.
- Bộ điều khiển nhiệt mới (mục "Bộ điều khiển nhiệt").
- Thêm linker script vào repo nên clone về là build được ngay.
- Giao diện mới dùng font làm mượt cạnh 16 mức xám (cả chữ nhỏ), biểu đồ trực tiếp, màn Sleep, màn chờ và trình hiệu chuẩn.

## Giấy phép

- Phần firmware gốc là của **imdolce** ([OpenStation-firmware](https://github.com/imdolce/OpenStation-firmware)), hiện chưa kèm file giấy phép. Dự án này chưa thêm giấy phép riêng cho tới khi có ý kiến của tác giả gốc.
- `src/st7735_reg.c`, `include/st7735_reg.h`: driver của STMicroelectronics (giữ nguyên ghi chú bản quyền trong file).
- Font và chữ OpenStation được vẽ từ DejaVu Sans, giấy phép Bitstream Vera: xem `licenses/DejaVu-fonts.txt`.

## Ghi công

- Phần cứng mạch "Trạm hàn v4": **toanthodien16** ([OSHWLAB](https://oshwlab.com/epubv3/project_rdzwfnjm)).
- Firmware gốc OpenStation 0.1.0 và hiệu chuẩn, PID của C245: **imdolce**.
- Ý tưởng giao diện và điều khiển: [AxxSolder](https://github.com/AxxAxx/AxxSolder).
- Bản C245/C210: **phiqd**, viết cùng Claude (Anthropic).
