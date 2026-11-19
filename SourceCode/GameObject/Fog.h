// ============================================================
// Fog.h
// 距離フォグパス（全画面）
// 深度バッファを読んでピクセルごとの距離を求め、
// Beer-Lambert の透過率で霧色をアルファ合成するパス
// 制作者：Chan WingChung
// ============================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

class CAMERA;

// ─── 深度バッファを読む距離フォグ用パス
class FOG
{
private:
    // ─── 定数バッファ
    struct CB_FOG_DATA
    {
        DirectX::XMFLOAT4X4 invViewProj;         // 64
        DirectX::XMFLOAT3   camPos;
        float               density;             // 16
        DirectX::XMFLOAT3   fogColor;
        float               heightFalloff;       // 16
        float               baseHeight;
        float               maxDistance;
        int                 marchSteps;
        float               scatterIntensity;    // 16
        float               phaseG;
        float               fogAmbient;
        float               resolutionScale;
        float               _pad0;           // 16
    };

    ID3D11Device* m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;

    Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_pVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_pPs;
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_pCb;
    Microsoft::WRL::ComPtr<ID3D11BlendState>        m_pBlendState;   // アルファ合成
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_pDepthState;   // 深度完全オフ
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_pRasterState;  // カリング無

    bool m_bInitialized = false;

    // ─── ImGui で調整するパラメータ
    float m_density          = 0.17f;
    float m_fogColor[3]      = { 0.65f, 0.70f, 0.75f };
    float m_heightFalloff    = 0.10f;      // 高さ減衰
    float m_baseHeight       = 0.0f;       // 霧が最も濃い高さ
    float m_maxDistance      = 24.0f;     // 空方向のマーチ距離
    int   m_marchSteps       = 48;         // マーチ回数
    float m_scatterIntensity = 2.0f;       // 散乱の強さ
    float m_phaseG           = 0.5f;       // 前方散乱の鋭さ
    float m_ambientStrength  = 0.06f;

    // ─── 半解像度用リソース
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_pHalfTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_pHalfRtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pHalfSrv;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>        m_pUpsamplePs;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_pLinearSampler;
    UINT m_halfWidth = 0;
    UINT m_halfHeight = 0;
    bool m_bHalfRes = true;    // ImGuiで切替（比較計測用）

public:
    bool Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
        UINT width, UINT height);   // ← 画面サイズを受け取る

    // ─── メイン描画後に呼ぶ：深度SRVを読み、RTVへフォグを合成
    void Execute(ID3D11RenderTargetView* pRtv,
        ID3D11ShaderResourceView* pDepthSrv,
        CAMERA* pCamera,
        UINT    width,
        UINT    height);

    void DrawImGui();
};