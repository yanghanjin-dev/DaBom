# DaBom

**버전 0.0.2** — Windows 네이티브 **C (Win32 + WIC)** 데스크톱 사진 뷰어입니다.  
이전 Electron/Vite/React 구현은 이 C 앱으로 대체되었습니다.

앱 버전은 `src/dabom.h`의 `DABOM_VERSION`이 단일 기준이며, 창 제목·빈 화면 부제에 `DaBom 0.0.2`로 표시됩니다.

### 0.0.2 포함

- 마우스 휠로 전체 보기 이전·다음
- **원본크기**(1:1) 맞춤
- UI 글자크기 독립 설정 (pt, `settings.ini`)
- Windows 앱 모드 다크/라이트 테마 연동

## 요구 사항

- Windows 10/11
- Visual Studio 2022/2026 (또는 Build Tools) — **C++를 사용한 데스크톱 개발** 워크로드  
  (`cl.exe`, Windows SDK)

## 빌드

프로젝트 루트에서:

```bat
cd C:\Users\yahaj\SynologyDrive\App\DaBom
build.bat
```

성공 시 실행 파일:

```text
C:\Users\yahaj\SynologyDrive\App\DaBom\build\dabom.exe
```

CMake를 쓸 경우:

```bat
cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64
cmake --build build-cmake --config Release
```

## 실행

```bat
build\dabom.exe
```

## 기능 (MVP)

1. **폴더 열기** / **파일 열기**(다중 선택)
2. 썸네일 그리드
3. 전체 보기: 이전·다음, ←→↑↓, Esc로 그리드 복귀
4. 확대·축소(툴바 버튼·`+`/`-` 키), **맞춤**(창에 맞춤), **원본크기**(1:1), 드래그 팬
5. 파일명·위치 표시, **슬라이드쇼**(Space 또는 버튼, 약 3초)
6. 마지막 폴더 기억: `%APPDATA%\DaBom\settings.ini`
7. **UI 글자크기** — Cursor Agents와 **무관**한 DaBom 전용 설정. 툴바·상태바·일반 UI는 **포인트(pt)** 기준(기본 **11 pt**). 브랜드 히로 제목은 UI pt의 약 3.5배로 함께 스케일됩니다.
8. **다크/라이트** — Windows **앱 모드**(설정 → 개인 설정 → 색 → 기본 앱 모드)를 따릅니다. Cursor와 무관합니다. 창 배경·툴바·상태바·빈 화면·텍스트·강조색이 모드별로 바뀌며, Windows에서 테마를 바꾸면 **즉시** 다시 그립니다(재시작 불필요). 다크일 때 제목 표시줄도 immersive dark를 시도합니다.

지원 형식: `jpg`, `jpeg`, `png`, `gif`, `webp`, `bmp` (HEIC 미지원)

### 다크 / 라이트 (Windows Apps 테마)

- 감지: 레지스트리 `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`의 `AppsUseLightTheme` (0=다크, 1=라이트). 가능하면 `uxtheme.dll`의 `ShouldAppsUseDarkMode`도 사용합니다.
- 라이브 갱신: `WM_SETTINGCHANGE` (`ImmersiveColorSet`) 및 `WM_THEMECHANGED`에서 브러시·색을 다시 만들고 `InvalidateRect`합니다.
- 선택: 다크 모드에서 `DwmSetWindowAttribute` immersive dark (제목 표시줄).

### UI 글자크기 (`settings.ini`)

Cursor 설정을 읽거나 동기화하지 **않습니다**. `%APPDATA%\DaBom\settings.ini`만 사용합니다.

```ini
[DaBom]
LastFolder=...
ui_font_size_pt=11
```

- 키: `ui_font_size_pt` (허용 범위 9–18, 기본 11)
- 앱 안에서도 툴바 **글자-** / **글자+**로 바꿀 수 있으며, 변경 시 같은 ini에 저장됩니다.
- GDI 높이: `lfHeight = -MulDiv(pt, LOGPIXELSY, 72)`

## 단축키

| 키 | 동작 |
|----|------|
| Esc | 전체 보기 → 그리드 |
| ← / → | 이전 / 다음 |
| 마우스 휠 (전체 보기) | 아래로 다음 / 위로 이전 |
| Space | 슬라이드쇼 토글 |
| + / - | 확대 / 축소 |
| 0 | 창에 맞춤 |
| 1 | 원본크기 (1:1) |
| Ctrl+O | 폴더 열기 |
| 글자- / 글자+ | UI 글자크기 조절 (ini에 저장) |

## 소스 구조

```text
src/dabom.h    공통 선언
src/main.c     창·UI·입력
src/image.c    WIC 이미지 로드 → HBITMAP
src/files.c    폴더 스캔·목록
src/config.c   %APPDATA% 설정
build.bat      MSVC 원클릭 빌드
CMakeLists.txt 선택적 CMake
```

## 기술 스택

- ISO C / MSVC C (`/TC`)
- Win32 API, GDI 표시
- Windows Imaging Component (WIC)
- Common Item Dialog (`IFileOpenDialog`)
