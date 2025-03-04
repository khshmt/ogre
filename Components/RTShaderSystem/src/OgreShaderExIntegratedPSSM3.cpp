/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreShaderPrecompiledHeaders.h"
#ifdef RTSHADER_SYSTEM_BUILD_EXT_SHADERS

#define SGX_LIB_INTEGRATEDPSSM                      "SGXLib_IntegratedPSSM"
#define SGX_FUNC_COMPUTE_SHADOW_COLOUR3             "SGX_ComputeShadowFactor_PSSM3"

namespace Ogre {
namespace RTShader {

/************************************************************************/
/*                                                                      */
/************************************************************************/
String IntegratedPSSM3::Type = "SGX_IntegratedPSSM3";
const String SRS_INTEGRATED_PSSM3 = "SGX_IntegratedPSSM3";

//-----------------------------------------------------------------------
IntegratedPSSM3::IntegratedPSSM3()
{
    mPCFxSamples = 2;
    mUseTextureCompare = false;
    mUseColourShadows = false;
    mDebug = false;
    mIsD3D9 = false;
    mShadowTextureParamsList.resize(1); // normal single texture depth shadowmapping
}

//-----------------------------------------------------------------------
int IntegratedPSSM3::getExecutionOrder() const
{
    return FFP_LIGHTING - 1;
}

//-----------------------------------------------------------------------
void IntegratedPSSM3::updateGpuProgramsParams(Renderable* rend, const Pass* pass,
                                             const AutoParamDataSource* source, 
                                             const LightList* pLightList)
{
    Vector4 vSplitPoints;

    for(size_t i = 0; i < mShadowTextureParamsList.size() - 1; i++)
    {
        vSplitPoints[i] = mShadowTextureParamsList[i].mMaxRange;
    }
    vSplitPoints[3] = mShadowTextureParamsList.back().mMaxRange;

    const Matrix4& proj = source->getProjectionMatrix();

    for(int i = 0; i < 4; i++)
    {
        auto tmp = proj * Vector4(0, 0, -vSplitPoints[i], 1);
        vSplitPoints[i] = tmp[2] / tmp[3];
    }


    mPSSplitPoints->setGpuParameter(vSplitPoints);

}

//-----------------------------------------------------------------------
void IntegratedPSSM3::copyFrom(const SubRenderState& rhs)
{
    const IntegratedPSSM3& rhsPssm= static_cast<const IntegratedPSSM3&>(rhs);

    mPCFxSamples = rhsPssm.mPCFxSamples;
    mUseTextureCompare = rhsPssm.mUseTextureCompare;
    mUseColourShadows = rhsPssm.mUseColourShadows;
    mDebug = rhsPssm.mDebug;
    mShadowTextureParamsList.resize(rhsPssm.mShadowTextureParamsList.size());

    ShadowTextureParamsConstIterator itSrc = rhsPssm.mShadowTextureParamsList.begin();
    ShadowTextureParamsIterator itDst = mShadowTextureParamsList.begin();

    while(itDst != mShadowTextureParamsList.end())
    {
        itDst->mMaxRange = itSrc->mMaxRange;        
        ++itSrc;
        ++itDst;
    }
}

//-----------------------------------------------------------------------
bool IntegratedPSSM3::preAddToRenderState(const RenderState* renderState, 
                                         Pass* srcPass, Pass* dstPass)
{
    if (!srcPass->getParent()->getParent()->getReceiveShadows() ||
        renderState->getLightCount().isZeroLength())
        return false;

    mIsD3D9 = ShaderGenerator::getSingleton().getTargetLanguage() == "hlsl" &&
              !GpuProgramManager::getSingleton().isSyntaxSupported("vs_4_0_level_9_1");

    PixelFormat shadowTexFormat = PF_UNKNOWN;
    const auto& configs = ShaderGenerator::getSingleton().getActiveSceneManager()->getShadowTextureConfigList();
    if (!configs.empty())
        shadowTexFormat = configs[0].format; // assume first texture is representative
    mUseTextureCompare = PixelUtil::isDepth(shadowTexFormat) && !mIsD3D9;
    mUseColourShadows = PixelUtil::getComponentType(shadowTexFormat) == PCT_BYTE; // use colour shadowmaps for byte textures

    ShadowTextureParamsIterator it = mShadowTextureParamsList.begin();

    while(it != mShadowTextureParamsList.end())
    {
        TextureUnitState* curShadowTexture = dstPass->createTextureUnitState();
            
        curShadowTexture->setContentType(TextureUnitState::CONTENT_SHADOW);
        curShadowTexture->setTextureAddressingMode(TextureUnitState::TAM_BORDER);
        curShadowTexture->setTextureBorderColour(ColourValue::White);
        if(mUseTextureCompare)
        {
            curShadowTexture->setTextureCompareEnabled(true);
            curShadowTexture->setTextureCompareFunction(CMPF_LESS_EQUAL);
        }
        it->mTextureSamplerIndex = dstPass->getNumTextureUnitStates() - 1;
        ++it;
    }

    

    return true;
}

//-----------------------------------------------------------------------
void IntegratedPSSM3::setSplitPoints(const SplitPointList& newSplitPoints)
{
    OgreAssert(newSplitPoints.size() <= 5, "at most 5 split points are supported");

    mShadowTextureParamsList.resize(newSplitPoints.size() - 1);

    for (size_t i = 1; i < newSplitPoints.size(); ++i)
    {
        mShadowTextureParamsList[i - 1].mMaxRange = newSplitPoints[i];
    }
}

bool IntegratedPSSM3::setParameter(const String& name, const String& value)
{
    if(name == "debug")
    {
        return StringConverter::parse(value, mDebug);
    }
    else if (name == "filter")
    {
        if(value == "pcf4")
            mPCFxSamples = 2;
        else if(value == "pcf16")
            mPCFxSamples = 4;
        else
            return false;
    }

    return false;
}

//-----------------------------------------------------------------------
bool IntegratedPSSM3::resolveParameters(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain = vsProgram->getEntryPointFunction();
    Function* psMain = psProgram->getEntryPointFunction();
    
    // Get input position parameter.
    mVSInPos = vsMain->getLocalParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
    if(!mVSInPos)
        mVSInPos = vsMain->getInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
    
    // Get output position parameter.
    mVSOutPos = vsMain->getOutputParameter(Parameter::SPC_POSITION_PROJECTIVE_SPACE);

    if (mIsD3D9)
    {
        mVSOutPos = vsMain->resolveOutputParameter(Parameter::SPC_UNKNOWN, GCT_FLOAT4);
    }

    // Resolve input depth parameter.
    mPSInDepth = psMain->resolveInputParameter(mVSOutPos);

    // Resolve computed local shadow colour parameter.
    mPSLocalShadowFactor = psMain->resolveLocalParameter(GCT_FLOAT1, "lShadowFactor");

    // Resolve computed local shadow colour parameter.
    mPSSplitPoints = psProgram->resolveParameter(GCT_FLOAT4, "pssm_split_points");
    
    ShadowTextureParamsIterator it = mShadowTextureParamsList.begin();
    int lightIndex = 0;

    while(it != mShadowTextureParamsList.end())
    {
        it->mWorldViewProjMatrix = vsProgram->resolveParameter(GpuProgramParameters::ACT_TEXTURE_WORLDVIEWPROJ_MATRIX, lightIndex);

        it->mVSOutLightPosition = vsMain->resolveOutputParameter(Parameter::Content(Parameter::SPC_POSITION_LIGHT_SPACE0 + lightIndex));        
        it->mPSInLightPosition = psMain->resolveInputParameter(it->mVSOutLightPosition);
        auto stype = mUseTextureCompare ? GCT_SAMPLER2DSHADOW : GCT_SAMPLER2D;
        it->mTextureSampler = psProgram->resolveParameter(stype, "shadow_map", it->mTextureSamplerIndex);
        it->mInvTextureSize = psProgram->resolveParameter(GpuProgramParameters::ACT_INVERSE_TEXTURE_SIZE,
                                                          it->mTextureSamplerIndex);

        ++lightIndex;
        ++it;
    }

    if (!(mVSInPos.get()) || !(mVSOutPos.get()))
    {
        OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Not all parameters could be constructed for the sub-render state.");
    }

    return true;
}

//-----------------------------------------------------------------------
bool IntegratedPSSM3::resolveDependencies(ProgramSet* programSet)
{
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    psProgram->addDependency(SGX_LIB_INTEGRATEDPSSM);

    psProgram->addPreprocessorDefines(StringUtil::format("PROJ_SPACE_SPLITS,PSSM_NUM_SPLITS=%zu,PCF_XSAMPLES=%.1f",
                                                         mShadowTextureParamsList.size(), mPCFxSamples));

    if(mDebug)
        psProgram->addPreprocessorDefines("DEBUG_PSSM");

    if(mUseTextureCompare)
        psProgram->addPreprocessorDefines("PSSM_SAMPLE_CMP");

    if(mUseColourShadows)
        psProgram->addPreprocessorDefines("PSSM_SAMPLE_COLOUR");

    return true;
}

//-----------------------------------------------------------------------
bool IntegratedPSSM3::addFunctionInvocations(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM); 
    Function* vsMain = vsProgram->getEntryPointFunction();  
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);

    // Add vertex shader invocations.
    if (false == addVSInvocation(vsMain, FFP_VS_TEXTURING + 1))
        return false;

    // Add pixel shader invocations.
    if (false == addPSInvocation(psProgram, FFP_PS_COLOUR_BEGIN))
        return false;

    return true;
}

//-----------------------------------------------------------------------
bool IntegratedPSSM3::addVSInvocation(Function* vsMain, const int groupOrder)
{
    auto stage = vsMain->getStage(groupOrder);

    if(mIsD3D9)
    {
        auto vsOutPos = vsMain->resolveOutputParameter(Parameter::SPC_POSITION_PROJECTIVE_SPACE);
        stage.assign(vsOutPos, mVSOutPos);
    }

    // Compute world space position.    
    ShadowTextureParamsIterator it = mShadowTextureParamsList.begin();

    while(it != mShadowTextureParamsList.end())
    {
        stage.callFunction(FFP_FUNC_TRANSFORM, it->mWorldViewProjMatrix, mVSInPos, it->mVSOutLightPosition);
        ++it;
    }

    return true;
}

//-----------------------------------------------------------------------
bool IntegratedPSSM3::addPSInvocation(Program* psProgram, const int groupOrder)
{
    Function* psMain = psProgram->getEntryPointFunction();
    auto stage = psMain->getStage(groupOrder);

    if(mShadowTextureParamsList.size() < 2)
    {
        ShadowTextureParams& splitParams0 = mShadowTextureParamsList[0];
        stage.callFunction("SGX_ShadowPCF4",
                           {In(splitParams0.mTextureSampler), In(splitParams0.mPSInLightPosition),
                            In(splitParams0.mInvTextureSize).xy(), Out(mPSLocalShadowFactor)});
    }
    else
    {
        auto fdepth = psMain->resolveLocalParameter(GCT_FLOAT1, "fdepth");
        if(mIsD3D9)
            stage.div(In(mPSInDepth).z(), In(mPSInDepth).w(), fdepth);
        else
            stage.assign(In(mPSInDepth).z(), fdepth);
        std::vector<Operand> params = {In(fdepth), In(mPSSplitPoints)};

        for(auto& texp : mShadowTextureParamsList)
        {
            params.push_back(In(texp.mPSInLightPosition));
            params.push_back(In(texp.mTextureSampler));
            params.push_back(In(texp.mInvTextureSize).xy());
        }

        params.push_back(Out(mPSLocalShadowFactor));

        // Compute shadow factor.
        stage.callFunction(SGX_FUNC_COMPUTE_SHADOW_COLOUR3, params);
    }

    // shadow factor is applied by lighting stages
    return true;
}

//-----------------------------------------------------------------------
SubRenderState* IntegratedPSSM3Factory::createInstance(ScriptCompiler* compiler, 
                                                      PropertyAbstractNode* prop, Pass* pass, SGScriptTranslator* translator)
{
    if (prop->name == "integrated_pssm4")
    {       
        if (prop->values.size() != 4)
        {
            compiler->addError(ScriptCompiler::CE_INVALIDPARAMETERS, prop->file, prop->line);
        }
        else
        {
            IntegratedPSSM3::SplitPointList splitPointList; 

            AbstractNodeList::const_iterator it = prop->values.begin();
            AbstractNodeList::const_iterator itEnd = prop->values.end();

            while(it != itEnd)
            {
                Real curSplitValue;
                
                if (false == SGScriptTranslator::getReal(*it, &curSplitValue))
                {
                    compiler->addError(ScriptCompiler::CE_INVALIDPARAMETERS, prop->file, prop->line);
                    break;
                }

                splitPointList.push_back(curSplitValue);

                ++it;
            }

            if (splitPointList.size() == 4)
            {
                SubRenderState* subRenderState = createOrRetrieveInstance(translator);
                IntegratedPSSM3* pssmSubRenderState = static_cast<IntegratedPSSM3*>(subRenderState);

                pssmSubRenderState->setSplitPoints(splitPointList);

                return pssmSubRenderState;
            }
        }       
    }

    return NULL;
}

//-----------------------------------------------------------------------
SubRenderState* IntegratedPSSM3Factory::createInstanceImpl()
{
    return OGRE_NEW IntegratedPSSM3;
}

}
}

#endif
