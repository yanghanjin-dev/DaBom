# DaBom 개발 및 릴리스 규칙

- 기능 개선 또는 버그 수정으로 동작이 바뀌면 버전을 올리고 `CHANGELOG.md`에 사용자 관점의 변경 사항을 기록한다.
- 초기 개발(0.x) 중 작은 기능 개선·수정은 patch 증가(예: 0.0.11 → 0.0.12), 큰 기능 묶음은 minor 증가, 호환성을 깨는 정식 변경은 major 증가를 기본으로 한다.
- `src/dabom.h`의 `DABOM_VERSION`, `DABOM_VERSION_W`, 루트 `VERSION`, `CMakeLists.txt`의 project 버전, README 현재 버전을 일치시킨다.
- 변경 후 Windows 빌드를 확인한다. 실행 검증을 하지 못했으면 명시한다.
- 사용자 요청에 따라 기능 개선을 배포할 때는 변경을 커밋·push하고 `v<버전>` 태그를 push하여 GitHub Release를 등록한다. 기존 공개 태그·Release는 덮어쓰지 않는다.
- `.github/workflows/release.yml`이 태그 소스를 빌드하고 실행 파일·ZIP·SHA256 체크섬과 변경 이력을 게시한다. 완료를 보고하기 전에 Actions와 Release 결과를 확인한다.
- 문서만 수정하거나 개발 도구만 변경하는 경우 앱 버전 증가는 필요하지 않다.

