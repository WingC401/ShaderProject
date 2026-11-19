// ============================================================
// ImguiManager.h
// Dear ImGui の初期化・フレーム管理・Win32 メッセージ処理のラッパー宣言
// 制作者：Chan WingChung
// ============================================================

#pragma once

#include <Windows.h>
#include <d3d11.h>

class IMGUI_MANAGER
{
private:
    // ─── 定数
    static constexpr float FONT_SIZE_PX = 16.0f;   // UI フォントサイズ（ピクセル）

    // ─── 状態
    bool m_bJapaneseFontLoaded = false;

    // ─── 内部ヘルパー
    void LoadJapaneseFont();

public:
    // ─── ライフサイクル
    bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
    void BeginFrame();
    void EndFrame();
    void Finalize();

    // ─── ゲッター
    bool IsJapaneseFontLoaded() const { return m_bJapaneseFontLoaded; }

    // ─── ウィンドウメッセージ処理
    static LRESULT HandleWindowMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};