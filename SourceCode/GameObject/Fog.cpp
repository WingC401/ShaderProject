// ============================================================
// Fog.cpp
// 距離フォグパスの実装
// 制作者：Chan WingChung
// ============================================================
#include "Fog.h"
#include "GameObject/Camera.h"
#include "Math/Matrix4x4.h"
#include "Imgui/imgui.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ------------------------------------------------------------
// Initialize
// シェーダー・ステート・定数バッファを生成する
// 引数：pDevice / pContext - D3D11 デバイスとコンテキスト
// ------------------------------------------------------------
bool FOG::Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
    UINT width, UINT height) 
{
    if (!pDevice || !pContext) return false;
    m_pDevice = pDevice;
    m_pContext = pContext;

    // ─── VS/PS を実行時コンパイル
    ComPtr<ID3DBlob> lVsBlob, lPsBlob, lErr;

    if (FAILED(D3DCompileFromFile(L"Shaders/FogVS.hlsl", nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &lVsBlob, &lErr)))
        return false;
    if (FAILED(m_pDevice->CreateVertexShader(
        lVsBlob->GetBufferPointer(), lVsBlob->GetBufferSize(), nullptr, &m_pVs)))
        return false;

    if (FAILED(D3DCompileFromFile(L"Shaders/FogPS.hlsl", nullptr, 
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main", "ps_5_0", 0, 0, &lPsBlob, &lErr)))
        return false;
    if (FAILED(m_pDevice->CreatePixelShader(
        lPsBlob->GetBufferPointer(), lPsBlob->GetBufferSize(), nullptr, &m_pPs)))
        return false;

    // ─── 定数バッファ
    D3D11_BUFFER_DESC lCbDesc = {};
    lCbDesc.ByteWidth = sizeof(CB_FOG_DATA);
    lCbDesc.Usage = D3D11_USAGE_DEFAULT;
    lCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_pDevice->CreateBuffer(&lCbDesc, nullptr, &m_pCb)))
        return false;

    // ─── アルファ合成（src.a, 1-src.a）雲パスと同じ
    D3D11_BLEND_DESC lBlendDesc = {};
    lBlendDesc.RenderTarget[0].BlendEnable = TRUE;
    lBlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    lBlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    lBlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    lBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    lBlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    lBlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    lBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_pDevice->CreateBlendState(&lBlendDesc, &m_pBlendState)))
        return false;

    // ─── 深度：完全オフ（DSVを外して描くため、テストも書き込みも不要）
    D3D11_DEPTH_STENCIL_DESC lDepthDesc = {};
    lDepthDesc.DepthEnable = FALSE;
    lDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(m_pDevice->CreateDepthStencilState(&lDepthDesc, &m_pDepthState)))
        return false;

    // ─── ラスタライザ：全画面三角形なのでカリング無効
    D3D11_RASTERIZER_DESC lRasterDesc = {};
    lRasterDesc.FillMode = D3D11_FILL_SOLID;
    lRasterDesc.CullMode = D3D11_CULL_NONE;
    if (FAILED(m_pDevice->CreateRasterizerState(&lRasterDesc, &m_pRasterState)))
        return false;
    // ─── アップサンプル用PS
    ComPtr<ID3DBlob> lUpBlob;
    if (FAILED(D3DCompileFromFile(L"Shaders/FogUpsamplePS.hlsl", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &lUpBlob, &lErr)))
        return false;
    if (FAILED(m_pDevice->CreatePixelShader(
        lUpBlob->GetBufferPointer(), lUpBlob->GetBufferSize(), nullptr, &m_pUpsamplePs)))
        return false;

    // ─── 半解像度RT（散乱色はHDRなのでfloat16フォーマット）
    m_halfWidth = width / 2;
    m_halfHeight = height / 2;

    D3D11_TEXTURE2D_DESC lHalfDesc = {};
    lHalfDesc.Width = m_halfWidth;
    lHalfDesc.Height = m_halfHeight;
    lHalfDesc.MipLevels = 1;
    lHalfDesc.ArraySize = 1;
    lHalfDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    lHalfDesc.SampleDesc = { 1, 0 };
    lHalfDesc.Usage = D3D11_USAGE_DEFAULT;
    lHalfDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_pDevice->CreateTexture2D(&lHalfDesc, nullptr, &m_pHalfTex)))    return false;
    if (FAILED(m_pDevice->CreateRenderTargetView(m_pHalfTex.Get(), nullptr, &m_pHalfRtv)))    return false;
    if (FAILED(m_pDevice->CreateShaderResourceView(m_pHalfTex.Get(), nullptr, &m_pHalfSrv)))  return false;

    // ─── 拡大用リニアサンプラー（クランプ）
    D3D11_SAMPLER_DESC lSampDesc = {};
    lSampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    lSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    lSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    lSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(m_pDevice->CreateSamplerState(&lSampDesc, &m_pLinearSampler)))    return false;

    m_bInitialized = true;
    return true;
}

// ------------------------------------------------------------
// Execute
// 深度SRVを読み、指定RTVへ距離フォグを全画面合成する
// 引数：pRtv         - 合成先レンダーターゲット
//       pDepthSrv    - メイン深度のSRV（Step 0 で作ったもの）
//       pCamera      - 逆ViewProjとカメラ位置の取得元
//       width/height - ビューポート
// ------------------------------------------------------------
void FOG::Execute(ID3D11RenderTargetView* pRtv, ID3D11ShaderResourceView* pDepthSrv,
    CAMERA* pCamera, UINT width, UINT height)
{
    if (!m_bInitialized || !pRtv || !pDepthSrv || !pCamera) return;

    // ─── 逆 ViewProj を計算（clip→world の復元用・雲パスと同じ）
    MATRIX4X4 lViewProj = pCamera->GetViewMatrix() * pCamera->GetProjectionMatrix();
    XMMATRIX  lInv = XMMatrixInverse(nullptr, lViewProj.ToXMMatrix());
    MATRIX4X4 lInvVp(lInv);

    // ─── 定数バッファ更新
    const VECTOR3& lPos = pCamera->GetPosition();

    CB_FOG_DATA lData = {};
    lData.invViewProj = lInvVp.Transpose().m_mat;
    lData.camPos = XMFLOAT3(lPos.x, lPos.y, lPos.z);
    lData.density = m_density;
    lData.fogColor = XMFLOAT3(m_fogColor[0], m_fogColor[1], m_fogColor[2]);
    lData.heightFalloff = m_heightFalloff;
    lData.baseHeight = m_baseHeight;
    lData.maxDistance = m_maxDistance;
    lData.marchSteps = m_marchSteps;
    lData.scatterIntensity = m_scatterIntensity;
    lData.phaseG = m_phaseG;
    lData.fogAmbient = m_ambientStrength;
    lData.resolutionScale = m_bHalfRes ? 2.0f : 1.0f;
    m_pContext->UpdateSubresource(m_pCb.Get(), 0, nullptr, &lData, 0, 0);

    // ─── DSVを外してRTVのみバインド（深度をSRVとして読むため同時バインド不可）
    m_pContext->OMSetRenderTargets(1, &pRtv, nullptr);

    // ─── 共通のIA/VS設定
    m_pContext->IASetInputLayout(nullptr);
    m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pContext->VSSetShader(m_pVs.Get(), nullptr, 0);
    m_pContext->RSSetState(m_pRasterState.Get());
    m_pContext->OMSetDepthStencilState(m_pDepthState.Get(), 0);

    ID3D11Buffer* lCb = m_pCb.Get();
    m_pContext->PSSetConstantBuffers(3, 1, &lCb);

    ID3D11ShaderResourceView* lSrvs[] = { pDepthSrv };
    m_pContext->PSSetShaderResources(1, 1, lSrvs);

    D3D11_VIEWPORT lVp = {};
    lVp.MaxDepth = 1.0f;
    float lBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (m_bHalfRes)
    {
        // ─── パス1：半解像度RTへフォグを描く（ブレンド無し・素の値を書く）
        const float lClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_pContext->ClearRenderTargetView(m_pHalfRtv.Get(), lClear);

        ID3D11RenderTargetView* lHalfRtv = m_pHalfRtv.Get();
        m_pContext->OMSetRenderTargets(1, &lHalfRtv, nullptr);
        lVp.Width = static_cast<float>(m_halfWidth);
        lVp.Height = static_cast<float>(m_halfHeight);
        m_pContext->RSSetViewports(1, &lVp);

        m_pContext->PSSetShader(m_pPs.Get(), nullptr, 0);
        m_pContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);   // 上書き
        m_pContext->Draw(3, 0);

        // ─── パス2：フル解像度へ拡大合成（ここでアルファブレンド）
        m_pContext->OMSetRenderTargets(1, &pRtv, nullptr);
        lVp.Width = static_cast<float>(width);
        lVp.Height = static_cast<float>(height);
        m_pContext->RSSetViewports(1, &lVp);

        ID3D11ShaderResourceView* lHalfSrv[] = { m_pHalfSrv.Get() };
        m_pContext->PSSetShaderResources(5, 1, lHalfSrv);
        ID3D11SamplerState* lSamp = m_pLinearSampler.Get();
        m_pContext->PSSetSamplers(5, 1, &lSamp);

        m_pContext->PSSetShader(m_pUpsamplePs.Get(), nullptr, 0);
        m_pContext->OMSetBlendState(m_pBlendState.Get(), lBlendFactor, 0xffffffff);
        m_pContext->Draw(3, 0);

        // ─── 半解像度SRVをアンバインド（次フレームでRTVとして使うため）
        ID3D11ShaderResourceView* lNullSrv5[] = { nullptr };
        m_pContext->PSSetShaderResources(5, 1, lNullSrv5);
    }
    else
    {
        // ─── 従来パス：直接バックバッファへ
        m_pContext->OMSetRenderTargets(1, &pRtv, nullptr);
        lVp.Width = static_cast<float>(width);
        lVp.Height = static_cast<float>(height);
        m_pContext->RSSetViewports(1, &lVp);

        m_pContext->PSSetShader(m_pPs.Get(), nullptr, 0);
        m_pContext->OMSetBlendState(m_pBlendState.Get(), lBlendFactor, 0xffffffff);
        m_pContext->Draw(3, 0);
    }

    // ─── 深度SRVをアンバインド（次パス・次フレームがDSVとして使えるように）
    ID3D11ShaderResourceView* lNullSrv[] = { nullptr };
    m_pContext->PSSetShaderResources(1, 1, lNullSrv);

    // ─── ステートを既定へ戻す
    m_pContext->OMSetBlendState(nullptr, nullptr,   0xffffffff);
    m_pContext->OMSetDepthStencilState(nullptr, 0);
}

// ------------------------------------------------------------
// DrawImGui
// フォグパラメータを実行時に調整するウィンドウ
// ------------------------------------------------------------
void FOG::DrawImGui()
{
    if (!m_bInitialized) return;

    if (ImGui::Begin("Fog"))
    {
        ImGui::SliderFloat("Density", &m_density, 0.0f, 0.2f, "%.4f");
        ImGui::ColorEdit3("Color", m_fogColor);
        ImGui::SliderFloat("Height Falloff", &m_heightFalloff, 0.01f, 1.0f, "%.3f");
        ImGui::SliderFloat("Base Height", &m_baseHeight, -20.0f, 50.0f);
        ImGui::SliderFloat("Max Distance", &m_maxDistance, 10.0f, 1000.0f);
        ImGui::SliderInt("March Steps", &m_marchSteps, 8, 64);
		ImGui::SliderFloat("Scatter Intensity", &m_scatterIntensity, 0.0f, 10.0f);
		ImGui::SliderFloat("Phase G", &m_phaseG, 0.0f, 0.95f);
        ImGui::SliderFloat("Ambient", &m_ambientStrength, 0.0f, 1.0f);
    }
    ImGui::End();
}