// ============================================================
// Capsule.h
// ストッキングシェーダー検証用のカプセル描画クラスの宣言
// 半径・高さ・分割数からカプセルメッシュを生成し、
// Phong（素肌）／Stocking（ストッキング合成）シェーダーで描画する
// 制作者：Chan WingChung
// ============================================================

#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <DirectXMath.h>

#include "GameObject.h"
#include "Camera.h"
#include "Light.h"
#include "Math/Vector3.h"
#include "Math/Matrix4x4.h"
#include "Math/Vertex.h"
#include "Math/ConstantBuffers.h"
#include "Shader/Shader.h"

using Microsoft::WRL::ComPtr;

class CAPSULE : public GAME_OBJECT
{
public:
    // ─── 描画に使うシェーダーの種類（ImGui のコンボと対応）
    enum class SHADER_MODE : int
    {
        PHONG = 0,      // 素肌のみ
        STOCKING = 1,   // 素肌 + ストッキング合成
    };

private:
    // ─── GPU リソース
    ComPtr<ID3D11Buffer> m_pVertexBuffer;
    ComPtr<ID3D11Buffer> m_pIndexBuffer;
    UINT                 m_indexCount = 0;

    ComPtr<ID3D11Buffer> m_pConstantBuffer;    // b0 : CB_WVP
    ComPtr<ID3D11Buffer> m_pCbStocking;        // b9 : CB_STOCKING
    ComPtr<ID3D11Buffer> m_pDissolveOffCb;     // b5 : ディゾルブ OFF 固定
    CB_WVP               m_cbData = {};
    CB_STOCKING          m_cbStocking = {};

    // ─── シェーダー（SHADER_MANAGER が所有、ここでは参照のみ）
    SHADER*     m_pSkinShader = nullptr;       // "Phong"
    SHADER*     m_pStockingShader = nullptr;   // "Stocking"
    SHADER_MODE m_shaderMode = SHADER_MODE::STOCKING;

    // ─── 依存オブジェクト
    CAMERA* m_pCamera = nullptr;
    LIGHT*  m_pLight = nullptr;

    // ─── 変換
    VECTOR3     m_position = { 0.0f, 1.0f, 0.0f };
    VECTOR3     m_rotation = { 0.0f, 0.0f, 0.0f };   // ラジアン
    std::string m_name = "Capsule";

    // ─── 形状パラメーター（変更時にメッシュを再生成する）
    float m_radius = 0.5f;      // 半球・円柱の半径
    float m_height = 1.2f;      // 円柱部分の長さ（全長 = height + 2 * radius）
    int   m_slices = 64;        // 円周方向の分割数
    int   m_stacks = 16;         // 半球 1 つあたりの緯度分割数
    bool  m_bGeometryDirty = false;

    // ─── マテリアル
    DirectX::XMFLOAT4 m_skinColor = { 0.91f, 0.65f, 0.47f, 1.0f };   // 肌色（線形）

    // ─── 内部ヘルパー
    bool      CreateGeometry();
    bool      InitConstantBuffer();
    MATRIX4X4 CalcWorldMatrix() const;
    SHADER*   SelectShader() const;
    void      DrawInternal(const MATRIX4X4* pOverrideViewProj, const VECTOR3* pOverrideEye);
    void      DrawImGuiStocking();
    void      DrawImGuiShape();

public:
    // ─── ライフサイクル
    bool Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext) override;
    void Update(float deltaSeconds = 0) override;
    void Draw()      override;
    void Finalize()  override {}
    void DrawImGui() override;
    void DrawWithViewProj(const MATRIX4X4& viewProj, const VECTOR3& eye) override;
    void DrawShadow(const MATRIX4X4& cubeViewProj, SHADER* pShadowShader);

    // ─── セッター（依存オブジェクト）
    void SetCamera(CAMERA* pCamera) { m_pCamera = pCamera; }
    void SetLight(LIGHT* pLight) override { m_pLight = pLight; }
    void SetSkinShader(SHADER* pShader) { m_pSkinShader = pShader; }
    void SetStockingShader(SHADER* pShader) { m_pStockingShader = pShader; }
    void SetShaderMode(SHADER_MODE mode) { m_shaderMode = mode; }

    // ─── セッター（変換・形状）
    void SetPosition(const VECTOR3& position) { m_position = position; }
    void SetRotation(const VECTOR3& rotation) { m_rotation = rotation; }
    void SetRadius(float radius) { m_radius = radius; m_bGeometryDirty = true; }
    void SetHeight(float height) { m_height = height; m_bGeometryDirty = true; }
    void SetSkinColor(const DirectX::XMFLOAT4& color) { m_skinColor = color; }

    // ─── セッター（ストッキング）
    void SetDenier(float denier) { m_cbStocking.denier = denier; }
    void SetStockingEnabled(bool bEnabled) { m_cbStocking.stockingEnabled = bEnabled ? 1 : 0; }

    // ─── ゲッター
    const VECTOR3&     GetPosition() const { return m_position; }
    const VECTOR3&     GetRotation() const { return m_rotation; }
    float              GetRadius()   const { return m_radius; }
    float              GetHeight()   const { return m_height; }
    float              GetDenier()   const { return m_cbStocking.denier; }
    SHADER_MODE        GetShaderMode() const { return m_shaderMode; }
    const std::string& GetName()     const { return m_name; }
};
