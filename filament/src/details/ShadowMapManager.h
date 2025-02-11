/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_DETAILS_SHADOWMAPMANAGER_H
#define TNT_FILAMENT_DETAILS_SHADOWMAPMANAGER_H

#include <filament/Viewport.h>

#include <private/backend/DriverApi.h>
#include <private/backend/DriverApiForward.h>
#include <private/backend/SamplerGroup.h>
#include <private/backend/SamplerGroup.h>

#include <private/filament/EngineEnums.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include "fg2/FrameGraph.h"

#include "TypedUniformBuffer.h"

#include <utils/FixedCapacityVector.h>

#include <math/vec3.h>

#include <array>
#include <memory>
#include <vector>

namespace filament {

class FView;

class ShadowMap;
class RenderPass;

class ShadowMapManager {
public:

    enum class ShadowTechnique : uint8_t {
        NONE = 0x0u,
        SHADOW_MAP = 0x1u,
        SCREEN_SPACE = 0x2u,
    };


    explicit ShadowMapManager(FEngine& engine);
    ~ShadowMapManager();

    // Reset shadow map layout.
    void reset() noexcept;

    void setShadowCascades(size_t lightIndex, size_t cascades) noexcept;
    void addSpotShadowMap(size_t lightIndex) noexcept;

    // Updates all of the shadow maps and performs culling.
    // Returns true if any of the shadow maps have visible shadows.
    ShadowTechnique update(FEngine& engine, FView& view,
            TypedUniformBuffer<PerViewUib>& perViewUb, TypedUniformBuffer<ShadowUib>& shadowUb,
            FScene::RenderableSoa& renderableData, FScene::LightSoa& lightData) noexcept;

    // Renders all of the shadow maps.
    void render(FrameGraph& fg, FEngine& engine, FView& view, backend::DriverApi& driver,
            RenderPass& pass) noexcept;

    const ShadowMap* getCascadeShadowMap(size_t c) const noexcept {
        assert_invariant(c < mCascadeShadowMapCache.size());
        return mCascadeShadowMapCache[c].get();
    }

private:

    struct ShadowLayout {
        LightManager::ShadowOptions const* options = nullptr;
        uint8_t layer = 0;
    };

    struct TextureRequirements {
        uint16_t size = 0;
        uint8_t layers = 0;
        uint8_t levels = 0;
    } mTextureRequirements;

    ShadowTechnique updateCascadeShadowMaps(FEngine& engine, FView& view,
            TypedUniformBuffer<PerViewUib>& perViewUb,
            FScene::RenderableSoa& renderableData, FScene::LightSoa& lightData) noexcept;
    ShadowTechnique updateSpotShadowMaps(FEngine& engine, FView& view,
            TypedUniformBuffer<ShadowUib>& shadowUb,
            FScene::RenderableSoa& renderableData, FScene::LightSoa& lightData) noexcept;

    void calculateTextureRequirements(FEngine& engine, FView& view, FScene::LightSoa& lightData) noexcept;

    class ShadowMapEntry {
    public:
        ShadowMapEntry() = default;
        ShadowMapEntry(ShadowMap* shadowMap, size_t light) :
                mShadowMap(shadowMap),
                mLightIndex(light) {
        }

        explicit operator bool() const { return mShadowMap != nullptr; }

        ShadowMap& getShadowMap() const { return *mShadowMap; }
        size_t getLightIndex() const { return mLightIndex; }
        const ShadowLayout& getLayout() const { return mLayout; }
        bool hasVisibleShadows() const { return mHasVisibleShadows; }

        void setHasVisibleShadows(bool hasVisibleShadows) { mHasVisibleShadows = hasVisibleShadows; }
        void setLayout(const ShadowLayout& layout) { mLayout = layout; }

    private:
        ShadowMap* mShadowMap = nullptr;
        ShadowLayout mLayout = {};
        size_t mLightIndex = 0; // TODO: does this need to be size_t?
        bool mHasVisibleShadows = false;
    };

    class CascadeSplits {
    public:
        constexpr static size_t SPLIT_COUNT = CONFIG_MAX_SHADOW_CASCADES + 1;

        struct Params {
            math::mat4f proj;
            float near = 0.0f;
            float far = 0.0f;
            size_t cascadeCount = 1;
            std::array<float, SPLIT_COUNT> splitPositions = { 0.0f };

            bool operator!=(const Params& rhs) const {
                return proj != rhs.proj ||
                       near != rhs.near ||
                       far != rhs.far ||
                       cascadeCount != rhs.cascadeCount ||
                       splitPositions != rhs.splitPositions;
            }
        };

        CascadeSplits() noexcept : CascadeSplits(Params{}) {}
        explicit CascadeSplits(Params const& params) noexcept;

        // Split positions in world-space.
        const float* beginWs() const { return mSplitsWs; }
        const float* endWs() const { return mSplitsWs + mSplitCount; }

        // Split positions in clip-space.
        const float* beginCs() const { return mSplitsCs; }
        const float* endCs() const { return mSplitsCs + mSplitCount; }

    private:
        float mSplitsWs[SPLIT_COUNT];
        float mSplitsCs[SPLIT_COUNT];
        size_t mSplitCount;
    };

    CascadeSplits::Params mCascadeSplitParams;
    CascadeSplits mCascadeSplits;

    // 16-bits seems enough.
    // TODO: make it an option.
    // TODO: iOS does not support the DEPTH16 texture format.
    backend::TextureFormat mTextureFormat = backend::TextureFormat::DEPTH16;
    float mTextureZResolution = 1.0f / (1u << 16u);

    utils::FixedCapacityVector<ShadowMapEntry> mCascadeShadowMaps{
            utils::FixedCapacityVector<ShadowMapEntry>::with_capacity(
                    CONFIG_MAX_SHADOW_CASCADES) };

    utils::FixedCapacityVector<ShadowMapEntry> mSpotShadowMaps{
            utils::FixedCapacityVector<ShadowMapEntry>::with_capacity(
                    CONFIG_MAX_SHADOW_CASTING_SPOTS) };

    std::array<std::unique_ptr<ShadowMap>, CONFIG_MAX_SHADOW_CASCADES> mCascadeShadowMapCache;
    std::array<std::unique_ptr<ShadowMap>, CONFIG_MAX_SHADOW_CASTING_SPOTS> mSpotShadowMapCache;
};

} // namespace filament

template<> struct utils::EnableBitMaskOperators<filament::ShadowMapManager::ShadowTechnique>
        : public std::true_type {};

#endif //TNT_FILAMENT_DETAILS_SHADOWMAPMANAGER_H
