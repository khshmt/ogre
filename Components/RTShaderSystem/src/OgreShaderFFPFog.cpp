/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

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
#ifdef RTSHADER_SYSTEM_BUILD_CORE_SHADERS

namespace Ogre {
namespace RTShader {

/************************************************************************/
/*                                                                      */
/************************************************************************/
const String SRS_FOG = "FFP_Fog";

//-----------------------------------------------------------------------
FFPFog::FFPFog()
{
    mFogMode                = FOG_NONE;
    mCalcMode               = CM_PER_VERTEX;
}

//-----------------------------------------------------------------------
const String& FFPFog::getType() const
{
    return SRS_FOG;
}

//-----------------------------------------------------------------------
int FFPFog::getExecutionOrder() const
{
    return FFP_FOG;
}

//-----------------------------------------------------------------------
bool FFPFog::resolveParameters(ProgramSet* programSet)
{
    if (mFogMode == FOG_NONE)
        return true;

    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain = vsProgram->getEntryPointFunction();
    Function* psMain = psProgram->getEntryPointFunction();
    
    // Resolve vertex shader output position.
    mVSOutPos = vsMain->resolveOutputParameter(Parameter::SPC_POSITION_PROJECTIVE_SPACE);
    
    // Resolve fog colour.
    mFogColour = psProgram->resolveParameter(GpuProgramParameters::ACT_FOG_COLOUR);
    
    // Resolve pixel shader output diffuse color.
    mPSOutDiffuse = psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
    
    // Per pixel fog.
    if (mCalcMode == CM_PER_PIXEL)
    {
        // Resolve fog params.      
        mFogParams = psProgram->resolveParameter(GpuProgramParameters::ACT_FOG_PARAMS);
        
        // Resolve vertex shader output depth.      
        mVSOutDepth = vsMain->resolveOutputParameter(Parameter::SPC_DEPTH_VIEW_SPACE);
        
        // Resolve pixel shader input depth.
        mPSInDepth = psMain->resolveInputParameter(mVSOutDepth);
    }
    // Per vertex fog.
    else
    {       
        // Resolve fog params.      
        mFogParams = vsProgram->resolveParameter(GpuProgramParameters::ACT_FOG_PARAMS);
        
        // Resolve vertex shader output fog factor.
        mVSOutFogFactor = vsMain->resolveOutputParameter(Parameter::SPC_UNKNOWN, GCT_FLOAT1);

        // Resolve pixel shader input fog factor.
        mPSInFogFactor = psMain->resolveInputParameter(mVSOutFogFactor);
    }

    return true;
}

//-----------------------------------------------------------------------
bool FFPFog::resolveDependencies(ProgramSet* programSet)
{
    if (mFogMode == FOG_NONE)
        return true;

    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);

    vsProgram->addDependency(FFP_LIB_FOG);
    psProgram->addDependency(FFP_LIB_COMMON);

    // Per pixel fog.
    if (mCalcMode == CM_PER_PIXEL)
    {
        psProgram->addDependency(FFP_LIB_FOG);

    }   

    return true;
}

//-----------------------------------------------------------------------
bool FFPFog::addFunctionInvocations(ProgramSet* programSet)
{
    Program* vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
    Program* psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
    Function* vsMain = vsProgram->getEntryPointFunction();
    Function* psMain = psProgram->getEntryPointFunction();

    const char* fogfunc = NULL;
    
    // Per pixel fog.
    if (mCalcMode == CM_PER_PIXEL)
    {
        vsMain->getStage(FFP_VS_FOG).assign(In(mVSOutPos).w(), mVSOutDepth);

        switch (mFogMode)
        {
        case FOG_LINEAR:
            fogfunc = FFP_FUNC_PIXELFOG_LINEAR;
            break;
        case FOG_EXP:
            fogfunc = FFP_FUNC_PIXELFOG_EXP;
            break;
        case FOG_EXP2:
            fogfunc = FFP_FUNC_PIXELFOG_EXP2;
            break;
        case FOG_NONE:
            return true;
        }
        psMain->getStage(FFP_PS_FOG)
            .callFunction(fogfunc,
                          {In(mPSInDepth), In(mFogParams), In(mFogColour), In(mPSOutDiffuse), Out(mPSOutDiffuse)});
    }

    // Per vertex fog.
    else
    {
        switch (mFogMode)
        {
        case FOG_LINEAR:
            fogfunc = FFP_FUNC_VERTEXFOG_LINEAR;
            break;
        case FOG_EXP:
            fogfunc = FFP_FUNC_VERTEXFOG_EXP;
            break;
        case FOG_EXP2:
            fogfunc = FFP_FUNC_VERTEXFOG_EXP2;
            break;
        case FOG_NONE:
            return true;
        }

        //! [func_invoc]
        auto vsFogStage = vsMain->getStage(FFP_VS_FOG);
        vsFogStage.callFunction(fogfunc, mVSOutPos, mFogParams, mVSOutFogFactor);
        //! [func_invoc]
        psMain->getStage(FFP_VS_FOG)
            .callFunction(FFP_FUNC_LERP, {In(mFogColour), In(mPSOutDiffuse), In(mPSInFogFactor), Out(mPSOutDiffuse)});
    }



    return true;
}

//-----------------------------------------------------------------------
void FFPFog::copyFrom(const SubRenderState& rhs)
{
    const FFPFog& rhsFog = static_cast<const FFPFog&>(rhs);

    mFogMode            = rhsFog.mFogMode;

    setCalcMode(rhsFog.mCalcMode);
}

//-----------------------------------------------------------------------
bool FFPFog::preAddToRenderState(const RenderState* renderState, Pass* srcPass, Pass* dstPass)
{
    if (srcPass->getFogOverride())
    {
        mFogMode         = srcPass->getFogMode();
    }
    else if(SceneManager* sceneMgr = ShaderGenerator::getSingleton().getActiveSceneManager())
    {
        mFogMode         = sceneMgr->getFogMode();
    }

    return mFogMode != FOG_NONE;
}

//-----------------------------------------------------------------------
bool FFPFog::setParameter(const String& name, const String& value)
{
	if(name == "calc_mode")
	{
        CalcMode cm = value == "per_vertex" ? CM_PER_VERTEX : CM_PER_PIXEL;
		setCalcMode(cm);
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------
const String& FFPFogFactory::getType() const
{
    return SRS_FOG;
}

//-----------------------------------------------------------------------
SubRenderState* FFPFogFactory::createInstance(ScriptCompiler* compiler, 
                                                    PropertyAbstractNode* prop, Pass* pass, SGScriptTranslator* translator)
{
    if (prop->name == "fog_stage")
    {
        if(prop->values.size() >= 1)
        {
            String strValue;

            if(false == SGScriptTranslator::getString(prop->values.front(), &strValue))
            {
                compiler->addError(ScriptCompiler::CE_INVALIDPARAMETERS, prop->file, prop->line);
                return NULL;
            }

            if (strValue == "ffp")
            {
                SubRenderState* subRenderState = createOrRetrieveInstance(translator);
                FFPFog* fogSubRenderState = static_cast<FFPFog*>(subRenderState);
                AbstractNodeList::const_iterator it = prop->values.begin();

                if(prop->values.size() >= 2)
                {
                    ++it;
                    if (false == SGScriptTranslator::getString(*it, &strValue))
                    {
                        compiler->addError(ScriptCompiler::CE_STRINGEXPECTED, prop->file, prop->line);
                        return NULL;
                    }

                    fogSubRenderState->setParameter("calc_mode", strValue);
                }
                
                return subRenderState;
            }
        }       
    }

    return NULL;
}

//-----------------------------------------------------------------------
void FFPFogFactory::writeInstance(MaterialSerializer* ser, SubRenderState* subRenderState, 
                                        Pass* srcPass, Pass* dstPass)
{
    ser->writeAttribute(4, "fog_stage");
    ser->writeValue("ffp");

    FFPFog* fogSubRenderState = static_cast<FFPFog*>(subRenderState);


    if (fogSubRenderState->getCalcMode() == FFPFog::CM_PER_VERTEX)
    {
        ser->writeValue("per_vertex");
    }
    else if (fogSubRenderState->getCalcMode() == FFPFog::CM_PER_PIXEL)
    {
        ser->writeValue("per_pixel");
    }
}

//-----------------------------------------------------------------------
SubRenderState* FFPFogFactory::createInstanceImpl()
{
    return OGRE_NEW FFPFog;
}

}
}

#endif
