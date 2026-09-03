# Orbit

Tiện ích hệ thống Windows (WinUI 3 / C++/WinRT), lấy cảm hứng từ [Mole](https://mole.fit) trên macOS: dọn cache, phân tích đĩa, quản lý phần mềm, tối ưu, và theo dõi trạng thái — một binary, không daemon nền.

Nguyên tắc: **quét trước, xóa sau**; Recycle Bin mặc định; whitelist; sizing có tính hardlink; không chạy cả app elevated.

## Tính năng hiện có

- **Clean (Earth)** — quét ~11 nhóm cache (User Temp, System Temp, Windows Update, Delivery Optimization, thumbnail, shader, trình duyệt, dev caches, WER, Prefetch chỉ xem, Recycle Bin). Review + chọn file, đưa vào Recycle Bin (hoặc xóa vĩnh viễn 2 bước). Progress bar khi dọn. Cap 50k file/nhóm để quét không bị treo.
- **Analyze (Jupiter)** — treemap dung lượng đĩa, chọn ổ, drill-down, tìm kiếm.
- **Software / Optimize / Status / Doctor** — có trong NavigationView, chưa làm xong trên `main`.
- **Settings** — gồm chế độ xóa (Recycle Bin / permanent).

## Yêu cầu

- Windows 10 17763+ hoặc Windows 11
- Visual Studio 2022 hoặc VS 18 (v143/v145) với workload C++ desktop + Windows SDK `10.0.26100`
- NuGet (`nuget restore` vì project dùng `packages.config`, không phải PackageReference)

## Build

```bat
nuget restore Orbit.sln
msbuild Orbit.sln /p:Configuration=Debug /p:Platform=x64
```

Hoặc mở `Orbit.sln` trong Visual Studio, cấu hình **Debug | x64**, nhấn **F5** (app packaged: lần đầu sẽ deploy MSIX rồi mở cửa sổ). Đừng chạy `Orbit.exe` trực tiếp từ thư mục `x64`.

`msbuild` thường không có trên PATH — dùng Developer PowerShell hoặc `MSBuild\Current\Bin\MSBuild.exe` trong thư mục cài VS.

## Cấu trúc

- `Orbit.sln` / `Orbit/Orbit.vcxproj` — single-project MSIX
- `Orbit/Core` — scanner, whitelist, protection, sizing
- `Orbit/Platform` — IFileOperation / Recycle Bin
- `Orbit/Views`, `Orbit/ViewModels` — WinUI pages
- `AGENTS.md` — ghi chú codegen C++/WinRT cho contributor

## License

Chưa công bố. Copyright hongducdev.

Open a PR. Do not commit packages/, Generated Files/, or *_h.h.
