# DaBom

**버전 0.0.8** — Windows 네이티브 **C (Win32 + WIC)** 데스크톱 사진 뷰어입니다.  
이전 Electron/Vite/React 구현은 이 C 앱으로 대체되었습니다.

앱 버전은 `src/dabom.h`의 `DABOM_VERSION`이 단일 기준이며, 창 제목·빈 화면 부제에 `DaBom 0.0.8`로 표시됩니다.

### 0.0.8 포함

- **공식 브랜드 로고·앱 아이콘** 적용
- `assets/` · `src/res/`에 수평 로고(다봄+DaBom)와 멀티 해상도 아이콘 PNG/ICO 보관
- `dabom.rc` `IDI_APPICON` → Explorer/작업표시줄/제목줄 아이콘 (`LoadImage` / `WM_SETICON` / 창 클래스)
- 빈 화면 히어로·프로그램 정보에 수평 로고 비트맵 (라이트/다크 테마별, Per-Monitor V2 고해상도)
- ICO 포함 크기: **16 / 20 / 24 / 32 / 40 / 48 / 64 / 96 / 128 / 256** (필수 16·32·48·256 포함)

### 0.0.7 포함

- **Per-Monitor DPI V2** 인식 (매니페스트 `dpiAwareness` + `SetProcessDpiAwarenessContext` 폴백)
- `WM_DPICHANGED`: 권장 사각형으로 창 크기 조정, NONCLIENTMETRICS 기반 폰트 재생성, 레이아웃/내비게이터/그리드 스케일
- 이미지 **1:1 = 장치 픽셀** 유지 · 맞춤보기는 클라이언트 픽셀 기준 · 공통 파일 대화상자도 선명(프로세스 DPI 상속)
- `build.bat` / CMake: `src/dabom.manifest`를 리소스로 임베드 (`rc` / `windres`)

### 0.0.6 포함

- **우클릭 컨텍스트 메뉴** (Apps 라이트/다크 owner-draw, 파일 메뉴와 동일 테마)
- 전체화면 · 연속보기 · 부드럽게(HALFTONE) · 이미지 배치 서브메뉴 · 복사/저장/회전/크기변경/포맷변환/인쇄
- 픽셀정보 · 카메라(EXIF) · 파일 속성 · 정렬 · HDR 미리보기 · 북마크 · 붙여넣기 · 탐색기 · 편집기 · 보관함 복사 · 바탕화면
- **이미지 배치**: 큰 이미지만 맞춤 / 화면·너비·높이 맞춤 / 원본 계속 / 배율 유지 / 늘리기 / 한장·두장·네장보기
- **Ctrl+마우스 휠** 줌(커서 기준) · 확대 시 **우하단 내비게이터**(닫기 X) · 상태줄 배율(%)
- 단축키: Alt+Enter 전체화면, Ctrl+E 포맷변환, **Ctrl+Shift+E** 편집 프로그램 등

### 0.0.5 포함

- **Win32 공통 컨트롤 상태바** (`STATUSCLASSNAME` / `CreateStatusWindow`)
- Apps 다크/라이트 상태바 owner-draw

### 0.0.4 포함

- **윈도우 프레임 메뉴바** (`SetMenu` / `HMENU`)
- **파일** 팝업: owner-draw Apps 라이트/다크

## 요구 사항

- Windows 10/11
- Visual Studio 2022/2026 (또는 Build Tools) — **C++를 사용한 데스크톱 개발**  
  (`cl.exe`, Windows SDK) — 없으면 MSYS2 gcc 폴백

## 빌드

```bat
cd C:\Users\yahaj\SynologyDrive\App\DaBom
build.bat
```

실행 파일:

```text
C:\Users\yahaj\SynologyDrive\App\DaBom\build\dabom.exe
```

## 기능

1. **파일** 메뉴 — 폴더/파일 열기 · 파일 정보 · 프로그램 정보 · 종료
2. 메뉴바: 이전 | 다음 | 축소 | 확대 | 맞춤 | 원본크기 | 슬라이드쇼
3. 썸네일 그리드 · 전체 보기
4. **우클릭 컨텍스트 메뉴** — 보기/편집/배치/정렬/북마크 등
5. Ctrl+휠 줌 + 내비게이터 · 일반 휠 이전/다음
6. 하단 상태바 — 파일명/인덱스 + 메타 + 배율
7. `%APPDATA%\DaBom\settings.ini` — LastFolder, SortMode, Layout, PageMode, Smooth, Bookmarks
8. **Per-Monitor DPI V2** — Explorer처럼 UI/대화상자 선명, 모니터 이동 시 자동 스케일
9. **브랜드 로고·앱 아이콘** — 빈 화면·About·exe 아이콘

지원 형식: `jpg`, `jpeg`, `png`, `gif`, `webp`, `bmp` (HEIC 미지원)

### 연속보기 동작

연속보기(S) ON이면 전체 보기에서 현재부터 이미지를 세로로 이어 붙이고 휠로 스크롤합니다. OFF면 한 장 + 휠로 이전/다음입니다.

### 단축키 (발췌)

| 키 | 동작 |
|----|------|
| Esc | 전체화면 종료 → 그리드 |
| Alt+Enter | 전체화면 토글 |
| Ctrl+휠 | 줌 인/아웃 (커서 기준) |
| 휠 | 이전/다음 (연속보기면 스크롤) |
| Ctrl+C / Ctrl+V | 복사 / 붙여넣기 |
| Ctrl+S | 다른 이름으로 저장 |
| Ctrl+T / Ctrl+R / Ctrl+E | 회전 / 크기변경 / 포맷변환 |
| Ctrl+Shift+E | 편집 프로그램 |
| Ctrl+P / Alt+P | 인쇄 / 픽셀정보 |
| Ctrl+Enter | 탐색기에서 선택 |
| Ins | 사진 보관함(`Pictures\DaBom`) 복사 |
| F12 | 카메라 정보 |
| Ctrl+Alt+1/2/4 | 한장 / 두장 / 네장보기 |
| Space | 슬라이드쇼 |
| + / - · 0 · 1 | 확대/축소 · 맞춤 · 원본크기 |

## 소스 구조

```text
assets/                 브랜드 원본 (로고·아이콘 PNG/ICO)
src/res/                RC 임베드용 복사본 (ico + 로고 PNG)
src/dabom.h             공통 선언 · DABOM_VERSION
src/main.c              창·UI·페인팅·입력·DPI·About
src/dabom.manifest      PerMonitorV2 + comctl32 v6
src/dabom.rc            매니페스트 · IDI_APPICON · 로고 RCDATA
src/ctxmenu.c           우클릭 컨텍스트 메뉴
src/actions.c           메뉴 액션(저장/인쇄/북마크 등)
src/image.c             WIC 로드·저장·회전·HDR·로고
src/files.c             폴더 스캔·정렬·페이지 비트맵
src/config.c            settings.ini
build.bat               MSVC/gcc 원클릭 빌드
```

## 기술 스택

- ISO C / MSVC C (`/TC`) 또는 MSYS2 gcc
- Win32 API, GDI 표시 (`AlphaBlend` 로고)
- Windows Imaging Component (WIC)
- Per-Monitor DPI Awareness V2
