// ============================================================
// Capsule.cpp
// ストッキングシェーダー検証用のカプセル描画クラスの実装
// 半径・高さ・分割数からカプセルメッシュを生成し、
// Phong（素肌）／Stocking（ストッキング合成）シェーダーで描画する
// シャドウパス・ミラーパスにも参加する
// 制作者：Chan WingChung
// ============================================================

#include "Capsule.h"
#include "Utility/Imgui/imgui.h"
#include <DirectXMath.h>
#include <cmath>

// ─── デバッグ表示モード名（CB_STOCKING::debugMode と対応）
static const char* DEBUG_MODE_NAMES[] =
{
    u8"最終結果",
    u8"カバレッジ",
    u8"NdotV",
    u8"シーンのみ",
    u8"つやのみ",
    u8"生地のみ",
    u8"ワールド法線",
};

// ------------------------------------------------------------
// Initialize
// ジオメトリと定数バッファを生成してストッキングの既定値を設定する
// 引数：pDevice  - D3D11 デバイスポインタ
//        pContext - D3D11 デバイスコンテキストポインタ
// ------------------------------------------------------------
bool CAPSULE::Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
    m_pDevice = pDevice;
    m_pContext = pContext;

    if (!CreateGeometry())     return false;
    if (!InitConstantBuffer()) return false;

    // ─── ストッキングの既定値（CB_STOCKING のデフォルトメンバ初期化子をそのまま使う）
    m_cbStocking = CB_STOCKING{};

    return true;
}

// ------------------------------------------------------------
// Update
// 形状パラメーターが変わっていればメッシュを再生成する
// 引数：deltaSeconds - 前フレームからの経過秒
// ------------------------------------------------------------
void CAPSULE::Update(float deltaSeconds)
{
    // ─── ImGui で半径・高さ・分割数が変更されたフレームの次に 1 回だけ作り直す
    if (m_bGeometryDirty)
    {
        CreateGeometry();
        m_bGeometryDirty = false;
    }
}

// ------------------------------------------------------------
// CreateGeometry
// Y 軸方向のカプセル（半球 + 円柱 + 半球）の頂点・インデックスを生成する
// 緯度リングを上極→上半球→円柱→下半球→下極の順に並べ、隣接リング間を四角形で結ぶ
// 引数：なし
// ------------------------------------------------------------
bool CAPSULE::CreateGeometry()
{
    const float lPi = DirectX::XM_PI;
    const int   lSlices = (m_slices < 3) ? 3 : m_slices;
    const int   lStacks = (m_stacks < 1) ? 1 : m_stacks;
    const float lHalfHeight = m_height * 0.5f;

    // ─── UV の V は「上極からの表面に沿った距離 / 全体の長さ」で割り当てる
    //      （半球の弧長 = radius * phi、円柱 = height）
    const float lTotalLength = lPi * m_radius + m_height;

    std::vector<VERTEX_3D> lVertices;
    std::vector<UINT>      lIndices;

    // ─── リングを 1 本追加するヘルパー
    //      phi      : 極からの角度（0 = 上極、PI = 下極）
    //      centerY  : そのリングが属する半球の中心 Y
    //      arcLen   : 上極からの表面距離（V 座標用）
    auto lAddRing = [&](float phi, float centerY, float arcLen)
        {
            const float lSinPhi = std::sin(phi);
            const float lCosPhi = std::cos(phi);
            const float lRingRadius = m_radius * lSinPhi;
            const float lY = centerY + m_radius * lCosPhi;
            const float lV = arcLen / lTotalLength;

            for (int lJ = 0; lJ <= lSlices; lJ++)
            {
                // 継ぎ目で UV が折り返さないよう、最後の頂点は最初と同じ位置で U = 1 にする
                const float lTheta = 2.0f * lPi * static_cast<float>(lJ) / static_cast<float>(lSlices);
                const float lCosTheta = std::cos(lTheta);
                const float lSinTheta = std::sin(lTheta);

                VERTEX_3D lVertex = {};
                lVertex.x = lRingRadius * lCosTheta;
                lVertex.y = lY;
                lVertex.z = lRingRadius * lSinTheta;

                // 法線 = 半球中心から頂点への方向（円柱部分では水平になる）
                lVertex.normalX = lSinPhi * lCosTheta;
                lVertex.normalY = lCosPhi;
                lVertex.normalZ = lSinPhi * lSinTheta;

                lVertex.u = static_cast<float>(lJ) / static_cast<float>(lSlices);
                lVertex.v = lV;

                // 接線 = 円周方向（theta が増える向き）
                lVertex.tangentX = -lSinTheta;
                lVertex.tangentY = 0.0f;
                lVertex.tangentZ = lCosTheta;

                // カプセルは元から滑らかなのでスムーズ法線 = 法線
                lVertex.smoothNormalX = lVertex.normalX;
                lVertex.smoothNormalY = lVertex.normalY;
                lVertex.smoothNormalZ = lVertex.normalZ;

                lVertices.push_back(lVertex);
            }
        };

    // ─── 上半球：phi = 0（極）→ PI/2（赤道）
    for (int lI = 0; lI <= lStacks; lI++)
    {
        const float lPhi = (lPi * 0.5f) * static_cast<float>(lI) / static_cast<float>(lStacks);
        lAddRing(lPhi, +lHalfHeight, m_radius * lPhi);
    }

    // ─── 下半球：phi = PI/2（赤道）→ PI（極）
    //      上半球の赤道リングとの間が円柱の側面になる
    for (int lI = 0; lI <= lStacks; lI++)
    {
        const float lPhi = (lPi * 0.5f) + (lPi * 0.5f) * static_cast<float>(lI) / static_cast<float>(lStacks);
        lAddRing(lPhi, -lHalfHeight, m_radius * (lPi * 0.5f) + m_height + m_radius * (lPhi - lPi * 0.5f));
    }

    // ─── 隣接リング間を四角形（2 三角形）で結ぶ
    //      時計回り = 表面（Cube.cpp と同じ巻き方）。裏返って見える場合は b/c を入れ替える
    const int lRingCount = (lStacks + 1) * 2;
    const int lRingStride = lSlices + 1;
    for (int lK = 0; lK < lRingCount - 1; lK++)
    {
        for (int lJ = 0; lJ < lSlices; lJ++)
        {
            const UINT lA = static_cast<UINT>(lK * lRingStride + lJ);           // 上・左
            const UINT lB = static_cast<UINT>(lK * lRingStride + lJ + 1);       // 上・右
            const UINT lC = static_cast<UINT>((lK + 1) * lRingStride + lJ);     // 下・左
            const UINT lD = static_cast<UINT>((lK + 1) * lRingStride + lJ + 1); // 下・右

            lIndices.push_back(lA); lIndices.push_back(lB); lIndices.push_back(lD);
            lIndices.push_back(lA); lIndices.push_back(lD); lIndices.push_back(lC);
        }
    }
    m_indexCount = static_cast<UINT>(lIndices.size());

    // ─── 頂点バッファを生成する（ComPtr の & は古いバッファを解放してから受け取る）
    D3D11_BUFFER_DESC lVbDesc = {};
    lVbDesc.ByteWidth = static_cast<UINT>(sizeof(VERTEX_3D) * lVertices.size());
    lVbDesc.Usage = D3D11_USAGE_DEFAULT;
    lVbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA lVbData = {};
    lVbData.pSysMem = lVertices.data();

    if (FAILED(m_pDevice->CreateBuffer(&lVbDesc, &lVbData, &m_pVertexBuffer)))
        return false;

    // ─── インデックスバッファを生成する
    D3D11_BUFFER_DESC lIbDesc = {};
    lIbDesc.ByteWidth = static_cast<UINT>(sizeof(UINT) * lIndices.size());
    lIbDesc.Usage = D3D11_USAGE_DEFAULT;
    lIbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA lIbData = {};
    lIbData.pSysMem = lIndices.data();

    if (FAILED(m_pDevice->CreateBuffer(&lIbDesc, &lIbData, &m_pIndexBuffer)))
        return false;

    return true;
}

// ------------------------------------------------------------
// InitConstantBuffer
// WVP 用・ストッキング用・ディゾルブ OFF 用の定数バッファを生成する
// 引数：なし
// ------------------------------------------------------------
bool CAPSULE::InitConstantBuffer()
{
    // ─── CB_WVP バッファを生成する（b0 スロット用）
    D3D11_BUFFER_DESC lCbDesc = {};
    lCbDesc.ByteWidth = sizeof(CB_WVP);
    lCbDesc.Usage = D3D11_USAGE_DEFAULT;
    lCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_pDevice->CreateBuffer(&lCbDesc, nullptr, &m_pConstantBuffer)))
        return false;

    // ─── CB_STOCKING バッファを生成する（b9 スロット用）
    D3D11_BUFFER_DESC lStockingDesc = {};
    lStockingDesc.ByteWidth = sizeof(CB_STOCKING);
    lStockingDesc.Usage = D3D11_USAGE_DEFAULT;
    lStockingDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_pDevice->CreateBuffer(&lStockingDesc, nullptr, &m_pCbStocking)))
        return false;

    // ─── ディゾルブ OFF 用 CB を生成する（モデルが残した b5 を上書きして消えないようにする）
    CB_DISSOLVE lDissolveOff = {};            // dissolveEnabled = 0
    D3D11_BUFFER_DESC lDisDesc = {};
    lDisDesc.ByteWidth = sizeof(CB_DISSOLVE);
    lDisDesc.Usage = D3D11_USAGE_DEFAULT;
    lDisDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA lDisData = { &lDissolveOff };
    if (FAILED(m_pDevice->CreateBuffer(&lDisDesc, &lDisData, &m_pDissolveOffCb)))
        return false;

    return true;
}

// ------------------------------------------------------------
// CalcWorldMatrix
// 回転・平行移動の合成ワールド行列を返す
// 半径・高さはメッシュ側に焼き込んであるのでスケールは使わない
// 引数：なし
// ------------------------------------------------------------
MATRIX4X4 CAPSULE::CalcWorldMatrix() const
{
    MATRIX4X4 lRot = MATRIX4X4::RotationX(m_rotation.x)
        * MATRIX4X4::RotationY(m_rotation.y)
        * MATRIX4X4::RotationZ(m_rotation.z);
    MATRIX4X4 lTrans = MATRIX4X4::Translation(m_position.x, m_position.y, m_position.z);
    return lRot * lTrans;
}

// ------------------------------------------------------------
// SelectShader
// ImGui で選ばれたモードに対応するシェーダーを返す（未設定なら他方で代用）
// 引数：なし
// ------------------------------------------------------------
SHADER* CAPSULE::SelectShader() const
{
    if (m_shaderMode == SHADER_MODE::STOCKING && m_pStockingShader)
        return m_pStockingShader;
    if (m_pSkinShader)
        return m_pSkinShader;
    return m_pStockingShader;
}

// ------------------------------------------------------------
// Draw
// 通常描画パスでカプセルを描画する
// 引数：なし
// ------------------------------------------------------------
void CAPSULE::Draw()
{
    DrawInternal(nullptr, nullptr);
}

// ------------------------------------------------------------
// DrawWithViewProj
// 反射パス用に外部の viewProj と eye でカプセルを描画する
// 引数：viewProj - 反射後のビュー × プロジェクション行列
//        eye      - 反射後のカメラ位置
// ------------------------------------------------------------
void CAPSULE::DrawWithViewProj(const MATRIX4X4& viewProj, const VECTOR3& eye)
{
    DrawInternal(&viewProj, &eye);
}

// ------------------------------------------------------------
// DrawShadow
// シャドウパスのキューブフェイス視点で深度のみ描画する（Cube と同じ手順）
// 引数：cubeViewProj  - そのフェイスのビュー×プロジェクション行列
//        pShadowShader - シャドウ生成シェーダー
// ------------------------------------------------------------
void CAPSULE::DrawShadow(const MATRIX4X4& cubeViewProj, SHADER* pShadowShader)
{
    if (!pShadowShader || !m_bVisible) return;

    pShadowShader->Bind(m_pContext);
    m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ─── world と WVP をキューブフェイスの視点で計算して CB に書き込む
    MATRIX4X4 lWorld = CalcWorldMatrix();
    m_cbData.world = lWorld.Transpose().m_mat;
    m_cbData.wvp = (lWorld * cubeViewProj).Transpose().m_mat;
    m_pContext->UpdateSubresource(m_pConstantBuffer.Get(), 0, nullptr, &m_cbData, 0, 0);

    // ─── VS にのみバインドする
    ID3D11Buffer* lCb = m_pConstantBuffer.Get();
    m_pContext->VSSetConstantBuffers(0, 1, &lCb);

    // ─── IA バッファをセットして描画する
    UINT lStride = sizeof(VERTEX_3D);
    UINT lOffset = 0;
    ID3D11Buffer* lVb = m_pVertexBuffer.Get();
    m_pContext->IASetVertexBuffers(0, 1, &lVb, &lStride, &lOffset);
    m_pContext->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_pContext->DrawIndexed(m_indexCount, 0, 0);
}

// ------------------------------------------------------------
// DrawInternal
// 選択中のシェーダーで CB を設定してカプセルを描画する内部関数
// 引数：pOverrideViewProj - 反射パス用 viewProj（nullptr なら通常カメラを使用）
//        pOverrideEye      - 反射パス用カメラ位置（nullptr なら通常カメラを使用）
// ------------------------------------------------------------
void CAPSULE::DrawInternal(const MATRIX4X4* pOverrideViewProj, const VECTOR3* pOverrideEye)
{
    SHADER* lShader = SelectShader();
    if (!m_pCamera || !lShader || !m_bVisible) return;

    lShader->Bind(m_pContext);
    m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ─── CB_WVP を設定する（反射パスの場合は override の行列を使用）
    MATRIX4X4 lWorld = CalcWorldMatrix();
    m_cbData.world = lWorld.Transpose().m_mat;
    m_cbData.wvp = pOverrideViewProj
        ? (lWorld * (*pOverrideViewProj)).Transpose().m_mat
        : m_pCamera->GetWVP(lWorld).m_mat;

    // ─── 肌色は materialColor に載せる（テクスチャなし・法線マップなし）
    m_cbData.materialColor = m_skinColor;
    m_cbData.useTexture = 0;
    m_cbData.useNormalMap = 0;

    const VECTOR3& lEye = pOverrideEye ? *pOverrideEye : m_pCamera->GetPosition();
    m_cbData.cameraPos = { lEye.x, lEye.y, lEye.z };

    m_cbData.useRimLight = 0;
    m_cbData.useToonLightTexture = 0;
    m_cbData.useToonDarkTexture = 0;

    m_pContext->UpdateSubresource(m_pConstantBuffer.Get(), 0, nullptr, &m_cbData, 0, 0);
    ID3D11Buffer* lCb = m_pConstantBuffer.Get();
    m_pContext->VSSetConstantBuffers(0, 1, &lCb);
    m_pContext->PSSetConstantBuffers(0, 1, &lCb);

    // ─── このオブジェクトはディゾルブしないので b5 を OFF で上書きする
    ID3D11Buffer* lDisOff = m_pDissolveOffCb.Get();
    m_pContext->PSSetConstantBuffers(5, 1, &lDisOff);

    // ─── ストッキングシェーダーのときだけ CB_STOCKING を b9 にバインドする
    if (lShader->RequiresExtraCb(SHADER::EXTRA_CB_STOCKING))
    {
        m_pContext->UpdateSubresource(m_pCbStocking.Get(), 0, nullptr, &m_cbStocking, 0, 0);
        ID3D11Buffer* lStockingCb = m_pCbStocking.Get();
        m_pContext->PSSetConstantBuffers(9, 1, &lStockingCb);
    }

    // ─── 頂点・インデックスバッファをセットして描画する
    UINT lStride = sizeof(VERTEX_3D);
    UINT lOffset = 0;
    ID3D11Buffer* lVb = m_pVertexBuffer.Get();
    m_pContext->IASetVertexBuffers(0, 1, &lVb, &lStride, &lOffset);
    m_pContext->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_pContext->DrawIndexed(m_indexCount, 0, 0);
}

// ------------------------------------------------------------
// DrawImGui
// 変換・形状・肌色・シェーダー切替・ストッキングパラメーターを編集する ImGui ウィンドウ
// 引数：なし
// ------------------------------------------------------------
void CAPSULE::DrawImGui()
{
    if (ImGui::Begin(u8"カプセル（ストッキング）"))
    {
        // ─── 変換パラメーターを編集する
        ImGui::Checkbox(u8"表示", &m_bVisible);
        ImGui::DragFloat3(u8"位置", &m_position.x, 0.1f);

        float lDeg[3] =
        {
            DirectX::XMConvertToDegrees(m_rotation.x),
            DirectX::XMConvertToDegrees(m_rotation.y),
            DirectX::XMConvertToDegrees(m_rotation.z)
        };
        if (ImGui::DragFloat3(u8"回転 (度)", lDeg, 1.0f))
        {
            m_rotation.x = DirectX::XMConvertToRadians(lDeg[0]);
            m_rotation.y = DirectX::XMConvertToRadians(lDeg[1]);
            m_rotation.z = DirectX::XMConvertToRadians(lDeg[2]);
        }

        ImGui::Separator();
        DrawImGuiShape();
        ImGui::Separator();

        // ─── 肌色とシェーダー切替
        ImGui::Text(u8"マテリアル");
        ImGui::ColorEdit3(u8"肌色（線形）", &m_skinColor.x);

        const char* lModeNames[] = { u8"Phong（素肌のみ）", u8"Stocking（ストッキング合成）" };
        int lMode = static_cast<int>(m_shaderMode);
        if (ImGui::Combo(u8"シェーダー", &lMode, lModeNames, 2))
            m_shaderMode = static_cast<SHADER_MODE>(lMode);

        if (m_shaderMode == SHADER_MODE::STOCKING && !m_pStockingShader)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), u8"Stocking シェーダーが未設定です（Phong で代用中）");

        // ─── ストッキングのパラメーター
        if (m_shaderMode == SHADER_MODE::STOCKING)
        {
            ImGui::Separator();
            DrawImGuiStocking();
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------
// DrawImGuiShape
// 半径・高さを編集する（変更があれば次の Update でメッシュを作り直す・分割数は固定）
// 引数：なし
// ------------------------------------------------------------
void CAPSULE::DrawImGuiShape()
{
    ImGui::Text(u8"形状");
    if (ImGui::DragFloat(u8"半径", &m_radius, 0.01f, 0.05f, 5.0f))  m_bGeometryDirty = true;
    if (ImGui::DragFloat(u8"高さ", &m_height, 0.01f, 0.0f, 10.0f))  m_bGeometryDirty = true;
}

// ------------------------------------------------------------
// DrawImGuiStocking
// CB_STOCKING の各パラメーターを編集するウィジェット群
// 引数：なし
// ------------------------------------------------------------
void CAPSULE::DrawImGuiStocking()
{
    ImGui::Text(u8"ストッキング");

    bool lbEnabled = (m_cbStocking.stockingEnabled != 0);
    if (ImGui::Checkbox(u8"有効", &lbEnabled))
        m_cbStocking.stockingEnabled = lbEnabled ? 1 : 0;

    // ─── デバッグ表示：各項を単体で見られる
    ImGui::Combo(u8"デバッグ表示", &m_cbStocking.debugMode,
        DEBUG_MODE_NAMES, static_cast<int>(sizeof(DEBUG_MODE_NAMES) / sizeof(DEBUG_MODE_NAMES[0])));

    ImGui::Spacing();

    // ─── 糸の密度と色
    ImGui::SliderFloat(u8"デニール", &m_cbStocking.denier, 5.0f, 200.0f, "%.0f D");
    if (ImGui::SmallButton("15D"))  m_cbStocking.denier = 15.0f;  ImGui::SameLine();
    if (ImGui::SmallButton("30D"))  m_cbStocking.denier = 30.0f;  ImGui::SameLine();
    if (ImGui::SmallButton("40D"))  m_cbStocking.denier = 40.0f;  ImGui::SameLine();
    if (ImGui::SmallButton("60D"))  m_cbStocking.denier = 60.0f;  ImGui::SameLine();
    if (ImGui::SmallButton("80D"))  m_cbStocking.denier = 80.0f;  ImGui::SameLine();
    if (ImGui::SmallButton("110D")) m_cbStocking.denier = 110.0f;
    ImGui::TextDisabled(u8"正面から見たカバレッジ = %.0f%%",
        (1.0f - std::exp(-m_cbStocking.denier / 60.0f)) * 100.0f);
    ImGui::ColorEdit3(u8"糸の色", &m_cbStocking.stockingColor.x);

    ImGui::Spacing();

    // ─── 糸の光り方：つや（異方性 GGX）とソフトシーン（Charlie）
    ImGui::SliderFloat(u8"つやの強さ", &m_cbStocking.glossIntensity, 0.0f, 5.0f);
    ImGui::SliderFloat(u8"つやのラフネス", &m_cbStocking.glossRoughness, 0.02f, 1.0f);
    ImGui::SliderFloat(u8"異方性", &m_cbStocking.anisotropy, -1.0f, 1.0f);
    ImGui::SliderFloat(u8"シーンの強さ", &m_cbStocking.sheenIntensity, 0.0f, 5.0f);
}
