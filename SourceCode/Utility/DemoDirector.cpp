// ============================================================
// DemoDirector.cpp
// デモモード用カメラ演出クラスの実装
// 制作者：Chan WingChung
// ============================================================
#include "DemoDirector.h"
#include "GameObject/Camera.h"
#include "Imgui/imgui.h"
#include <cstdio>
#include <string>

namespace
{
    // ─── キーフレームの保存先
    const char* DEMO_FILE_PATH = "Assets/demo_camera.txt";
}

// ------------------------------------------------------------
// SmoothStep
// 0〜1 の進行度をイーズイン・アウト曲線に変換する
// 引数：t - 線形の進行度（0〜1）
// ------------------------------------------------------------
float DEMO_DIRECTOR::SmoothStep(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// ------------------------------------------------------------
// CaptureFrom
// 現在のカメラ状態から1キーフレームを作る
// 引数：pCamera  - 取得元カメラ
//       duration - このキーへ到達するまでの秒数
// ------------------------------------------------------------
DEMO_KEYFRAME DEMO_DIRECTOR::CaptureFrom(CAMERA* pCamera, float duration) const
{
    DEMO_KEYFRAME lKey = {};
    lKey.target = pCamera->GetTarget();
    lKey.distance = pCamera->GetDistance();
    lKey.yaw = pCamera->GetYaw();
    lKey.pitch = pCamera->GetPitch();
    lKey.duration = duration;
    return lKey;
}

// ------------------------------------------------------------
// Play / Stop
// ------------------------------------------------------------
void DEMO_DIRECTOR::Play()
{
    if (m_keyframes.size() < 2) return;   // 2キー未満は再生不可
    m_time = 0.0f;
    m_bPlaying = true;
}

void DEMO_DIRECTOR::Stop()
{
    m_bPlaying = false;
}

// ------------------------------------------------------------
// Update
// 経過時間から現在区間を求め、カメラをイーズ補間で駆動する
// 引数：deltaSeconds - 前フレームからの経過秒
//       pCamera      - 駆動対象カメラ
// ------------------------------------------------------------
void DEMO_DIRECTOR::Update(float deltaSeconds, CAMERA* pCamera)
{
    if (!m_bPlaying || !pCamera || m_keyframes.size() < 2) return;

    m_time += deltaSeconds;

    // ─── 総再生時間（キー1以降の duration の合計）
    float lTotal = 0.0f;
    for (size_t lI = 1; lI < m_keyframes.size(); ++lI)
        lTotal += m_keyframes[lI].duration;

    // ─── 末尾に達したらループ or 停止
    if (m_time >= lTotal)
    {
        if (m_bLoop)
            m_time = fmodf(m_time, lTotal);
        else
        {
            m_time = lTotal;
            m_bPlaying = false;
        }
    }

    // ─── 現在の区間（キー lIdx-1 → lIdx）を探す
    float  lSegStart = 0.0f;
    size_t lIdx = 1;
    for (; lIdx < m_keyframes.size() - 1; ++lIdx)
    {
        if (m_time < lSegStart + m_keyframes[lIdx].duration)
            break;
        lSegStart += m_keyframes[lIdx].duration;
    }

    const DEMO_KEYFRAME& lA = m_keyframes[lIdx - 1];
    const DEMO_KEYFRAME& lB = m_keyframes[lIdx];

    // ─── 区間内の進行度をイーズしてから各パラメータを線形補間
    float lDur = (lB.duration > 0.001f) ? lB.duration : 0.001f;
    float lT = SmoothStep((m_time - lSegStart) / lDur);

    VECTOR3 lTarget;
    lTarget.x = lA.target.x + (lB.target.x - lA.target.x) * lT;
    lTarget.y = lA.target.y + (lB.target.y - lA.target.y) * lT;
    lTarget.z = lA.target.z + (lB.target.z - lA.target.z) * lT;

    pCamera->SetTarget(lTarget);
    pCamera->SetOrbit(
        lA.distance + (lB.distance - lA.distance) * lT,
        lA.yaw + (lB.yaw - lA.yaw) * lT,
        lA.pitch + (lB.pitch - lA.pitch) * lT);
}

// ------------------------------------------------------------
// Save / Load
// キーフレームをテキスト形式で保存・復元する
// ------------------------------------------------------------
void DEMO_DIRECTOR::Save()
{
    FILE* lFp = nullptr;
    if (fopen_s(&lFp, DEMO_FILE_PATH, "w") != 0 || !lFp) return;

    for (const auto& lKey : m_keyframes)
    {
        fprintf(lFp, "%f %f %f %f %f %f %f\n",
            lKey.target.x, lKey.target.y, lKey.target.z,
            lKey.distance, lKey.yaw, lKey.pitch, lKey.duration);
    }
    fclose(lFp);
}

void DEMO_DIRECTOR::Load()
{
    FILE* lFp = nullptr;
    if (fopen_s(&lFp, DEMO_FILE_PATH, "r") != 0 || !lFp) return;

    m_keyframes.clear();
    DEMO_KEYFRAME lKey = {};
    while (fscanf_s(lFp, "%f %f %f %f %f %f %f",
        &lKey.target.x, &lKey.target.y, &lKey.target.z,
        &lKey.distance, &lKey.yaw, &lKey.pitch, &lKey.duration) == 7)
    {
        m_keyframes.push_back(lKey);
    }
    fclose(lFp);
    m_selectedIdx = 0;
}

// ------------------------------------------------------------
// DrawImGui
// キーの記録・編集・再生操作ウィンドウ
// 引数：pCamera - キー記録元／プレビュー対象のカメラ
// ------------------------------------------------------------
void DEMO_DIRECTOR::DrawImGui(CAMERA* pCamera)
{
    if (!ImGui::Begin("Demo Director"))
    {
        ImGui::End();
        return;
    }

    // ─── 再生コントロール
    if (m_bPlaying)
    {
        if (ImGui::Button("Stop (F2)")) Stop();
    }
    else
    {
        if (ImGui::Button("Play (F2)")) Play();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_bLoop);
    ImGui::Text("Keys: %d  Time: %.1fs", static_cast<int>(m_keyframes.size()), m_time);

    ImGui::Separator();

    // ─── キーの記録（今のカメラをそのまま1構図として保存）
    if (ImGui::Button("Add Keyframe") && pCamera)
    {
        m_keyframes.push_back(CaptureFrom(pCamera, 3.0f));
        m_selectedIdx = static_cast<int>(m_keyframes.size()) - 1;
    }
    ImGui::SameLine();

    bool lHasSelection = (m_selectedIdx >= 0
        && m_selectedIdx < static_cast<int>(m_keyframes.size()));

    if (ImGui::Button("Update Sel") && pCamera && lHasSelection)
    {
        float lDur = m_keyframes[m_selectedIdx].duration;
        m_keyframes[m_selectedIdx] = CaptureFrom(pCamera, lDur);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Sel") && lHasSelection)
    {
        m_keyframes.erase(m_keyframes.begin() + m_selectedIdx);
        if (m_selectedIdx >= static_cast<int>(m_keyframes.size()))
            m_selectedIdx = static_cast<int>(m_keyframes.size()) - 1;
    }

    // ─── キー一覧
    for (int lI = 0; lI < static_cast<int>(m_keyframes.size()); ++lI)
    {
        std::string lLabel = "Key" + std::to_string(lI)
            + " (" + std::to_string(m_keyframes[lI].duration) + "s)";
        if (ImGui::Selectable(lLabel.c_str(), m_selectedIdx == lI))
            m_selectedIdx = lI;
    }

    // ─── 選択キーの所要秒数
    if (lHasSelection)
        ImGui::DragFloat("Duration", &m_keyframes[m_selectedIdx].duration,
            0.1f, 0.5f, 30.0f, "%.1fs");

    ImGui::Separator();

    // ─── 保存・読み込み
    if (ImGui::Button("Save")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Load")) Load();

    ImGui::End();
}