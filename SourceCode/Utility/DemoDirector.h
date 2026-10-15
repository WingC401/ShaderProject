// ============================================================
// DemoDirector.h
// デモモード用カメラ演出クラスの宣言
// キーフレーム（注視点・距離・角度）を記録・補間再生する
// 制作者：Chan WingChung
// ============================================================
#pragma once

#include <vector>
#include "Math/Vector3.h"

class CAMERA;

// ─── カメラの1構図（前のキーからここまで duration 秒かけて移動）
struct DEMO_KEYFRAME
{
    VECTOR3 target;
    float   distance;
    float   yaw;
    float   pitch;
    float   duration;
};

// ─── キーフレームの記録・保存・補間再生を担うデモ演出クラス
class DEMO_DIRECTOR
{
private:
    std::vector<DEMO_KEYFRAME> m_keyframes;
    bool  m_bPlaying = false;
    bool  m_bLoop = true;
    float m_time = 0.0f;
    int   m_selectedIdx = 0;

    // ─── 区間内の進行度をイーズイン・アウトに変換する
    static float SmoothStep(float t);

    // ─── 現在カメラから1キーを作る
    DEMO_KEYFRAME CaptureFrom(CAMERA* pCamera, float duration) const;

public:
    // ─── 再生中ならカメラを補間駆動する（GameObjectManager::Update の前に呼ぶ）
    void Update(float deltaSeconds, CAMERA* pCamera);
    void DrawImGui(CAMERA* pCamera);

    // ─── 再生制御
    void Play();
    void Stop();
    bool IsPlaying() const { return m_bPlaying; }

    // ─── キーフレームのファイル保存・読み込み
    void Save();
    void Load();
};