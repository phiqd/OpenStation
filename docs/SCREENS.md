# Các màn hình

Ảnh được vẽ bằng chính code giao diện của firmware, chạy trong bộ mô phỏng trên máy tính (`sim/run.sh`) với mô hình nhiệt của mũi C245 ở nguồn 24 V. Màn thật 160×128 điểm ảnh; ảnh phóng to 3 lần (ảnh động 2 lần).

## Ảnh động

| Số nhiệt chạy khi mũi đang nóng lên | Đổi nhiệt đặt 300 → 310 °C (chỉ chữ số hàng chục trượt) |
| --- | --- |
| <img src="images/anim_heatup_rolling.gif" width="320"> | <img src="images/anim_set_rolling.gif" width="320"> |

Khi nhiệt thay đổi nhanh (làm nóng, nguội), số trên màn hình cập nhật khoảng 4 lần mỗi giây và trượt nhanh hơn, để mỗi giá trị đứng yên đủ lâu cho mắt đọc. Khi đang giữ nhiệt, dao động ±1 °C ngắn hơn 0,4 giây không được hiển thị.

## Khởi động

| Bật nguồn, đã cắm mũi | Bật nguồn, chưa cắm mũi |
| --- | --- |
| <img src="images/01_splash.png" width="320"> | <img src="images/01b_splash_no_tip.png" width="320"> |

Hiện 3,5 giây (bấm để bỏ qua), sau đó vào màn hình chính.

## Màn hình chính

| Đang tắt (OFF) | Đang nóng lên (HEAT) |
| --- | --- |
| <img src="images/02_main_off.png" width="320"> | <img src="images/03a_main_heating.png" width="320"> |
| **Đã đạt nhiệt (READY)** | **Xoay núm: SET vàng = chưa lưu, bấm 1 lần để lưu** |
| <img src="images/03_main_ready.png" width="320"> | <img src="images/03b_set_unsaved.png" width="320"> |

## Boost

| Đang nóng lên tới nhiệt Boost | Đã đạt, đếm ngược ở ô bên phải |
| --- | --- |
| <img src="images/04a_boost_heating.png" width="320"> | <img src="images/04_main_boost.png" width="320"> |

## Sleep

| Vừa gác lên giá: đếm ngược tới lúc ngủ | Đang ngủ: giữ nhiệt thấp, đếm ngược tới lúc tắt nung |
| --- | --- |
| <img src="images/05_sleep_countdown.png" width="320"> | <img src="images/06_sleep.png" width="320"> |

## Khi tắt nung

| Mũi còn nóng | Mũi đã nguội | Hết giờ Sleep (đèn nền dịu) |
| --- | --- | --- |
| <img src="images/07a_logo_hot.png" width="250"> | <img src="images/07b_logo_off.png" width="250"> | <img src="images/07c_logo_after_sleep.png" width="250"> |

## Cài đặt

| Trang 1 | Trang 2 |
| --- | --- |
| <img src="images/10a_settings.png" width="320"> | <img src="images/10b_settings_page2.png" width="320"> |
| **Trang 3** | **Đang sửa một mục (viền cam)** |
| <img src="images/10c_settings_page3.png" width="320"> | <img src="images/10d_settings_edit.png" width="320"> |

Giữ núm 0,8 giây để vào hoặc thoát. Xoay để chọn, bấm để sửa, xoay để đổi giá trị, bấm để xong.

## Hiệu chuẩn tay hàn

| Hướng dẫn | Đang hiệu chuẩn (bước 1/6) |
| --- | --- |
| <img src="images/11a_cal_intro.png" width="320"> | <img src="images/11b_cal_wizard.png" width="320"> |

## Khác

| Thông tin (About) | Báo lỗi |
| --- | --- |
| <img src="images/12_about.png" width="320"> | <img src="images/13_fault.png" width="320"> |

## Bộ điều khiển nhiệt (mô phỏng)

<img src="images/pid_compare.png" width="100%">

So sánh PID cũ và mới khi làm nóng và khi hàn mối lớn, ở nguồn 24 V và 12 V. Chi tiết trong [README](../README.md#bộ-điều-khiển-nhiệt).
