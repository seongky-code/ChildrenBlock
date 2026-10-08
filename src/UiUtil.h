#pragma once

#include "Protocol.h"

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_UNLOCK_REQUEST = WM_APP + 2;

// 96 DPI 기준 값을 현재 시스템 DPI 에 맞게 변환
int ScaleForDpi(int value);

// 대화 상자 제목 표시줄/작업 표시줄에 프로그램 아이콘 적용
void ApplyAppIcon(CWnd* wnd);

// 항상 위 메시지 상자
int ShowMessage(CWnd* owner, LPCTSTR text, UINT flags);

// 서비스에 요청. 연결 실패나 ERR 응답이면 알맞은 메시지를 보여 주고 false.
bool CallService(CWnd* owner, const proto::Message& request, proto::Message* response = nullptr);

// 비밀번호 입력 대화 상자. 취소하면 false.
bool AskPassword(CWnd* owner, const CString& prompt, CString& password);

// 비밀번호를 물은 뒤 pw 를 붙여 요청
bool CallProtected(CWnd* owner, const CString& prompt, proto::Message request, proto::Message* response = nullptr);
