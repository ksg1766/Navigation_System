#include "Client_Shader_Defines.hlsl"
#include "Shader_Lighting_Functions.hlsl"

#define MAX_INSTANCE 500

/* 상수테이블. */
matrix			g_ViewMatrix, g_ProjMatrix;
Texture2D		g_Texture;

vector g_vLightDir = vector(1.f, -1.f, 1.f, 0.f);
vector g_vLightEmissive = vector(1.f, 1.f, 1.f, 1.f);

vector g_vMtrlEmissive = vector(1.f, 1.f, 0.1f, 1.f);

vector g_vCamPosition;

struct VS_IN
{
	/* 그리기용 정점. m_pVB */
	float3		vPosition : POSITION;
	float2		vPSize : PSIZE;
	
    row_major matrix matWorld : INST;
};

struct VS_OUT
{
	/* float4 : w값을 반드시 남겨야하는 이유는 w에 뷰스페이스 상의 깊이(z)를 보관하기 위해서다. */
	float4		vPosition : POSITION;
	float2		vPSize : PSIZE;
};

/* 버텍스에 대한 변환작업을 거친다.  */
/* 변환작업 : 정점의 위치에 월드, 뷰, 투영행렬을 곱한다. and 필요한 변환에 대해서 자유롭게 추가해도 상관없다 .*/
/* 버텍스의 구성 정보를 변경한다. */
VS_OUT VS_MAIN(/* 정점 */VS_IN In)
{
	VS_OUT			Out = (VS_OUT)0;	

    vector vRight = In.matWorld._11_12_13_14;
    vector vUp = In.matWorld._21_22_23_24;

	//vector		vPosition = mul(float4(In.vPosition, 1.f), In.matWorld);

    Out.vPosition = mul(float4(In.vPosition, 1.f), In.matWorld);
	Out.vPSize = float2(In.vPSize.x * length(vRight), In.vPSize.y * length(vUp));

	return Out;	
}

struct GS_IN
{	
	float4		vPosition : POSITION;
	float2		vPSize : PSIZE;
};

struct GS_OUT
{
	float4		vPosition : SV_POSITION;
    //float4		vNormal : NORMAL;
	float2		vTexcoord : TEXCOORD0;
    //float4		vProjPos : TEXCOORD1;
};

/* 여러개의 정점을 생성한다. */
[maxvertexcount(20)]
void GS_MAIN(point GS_IN In[1], inout TriangleStream<GS_OUT> OutStream)
{
	GS_OUT		Out[4];


	/* 받아온 정점을 기준으로하여 사각형을 구성하기위한 정점 여섯개를 만들거야. */
	vector		vLook = g_vCamPosition - In[0].vPosition;

	float3		vRight = normalize(cross(float3(0.f, 1.f, 0.f), vLook.xyz)) * In[0].vPSize.x * 0.5f;
	float3		vUp = normalize(cross(vLook.xyz, vRight.xyz)) * In[0].vPSize.y * 0.5f;

	matrix		matVP;

	matVP = mul(g_ViewMatrix, g_ProjMatrix);

	Out[0].vPosition = vector(In[0].vPosition.xyz + vRight + vUp, 1.f);
	Out[0].vPosition = mul(Out[0].vPosition, matVP);
	Out[0].vTexcoord = float2(0.0f, 0.f);
    //Out[0].vProjPos = Out[0].vPosition;

	Out[1].vPosition = vector(In[0].vPosition.xyz - vRight + vUp, 1.f);
	Out[1].vPosition = mul(Out[1].vPosition, matVP);
	Out[1].vTexcoord = float2(1.0f, 0.f);
    //Out[1].vProjPos = Out[1].vPosition;

	Out[2].vPosition = vector(In[0].vPosition.xyz - vRight - vUp, 1.f);
	Out[2].vPosition = mul(Out[2].vPosition, matVP);
	Out[2].vTexcoord = float2(1.0f, 1.0f);
    //Out[2].vProjPos = Out[2].vPosition;

	Out[3].vPosition = vector(In[0].vPosition.xyz + vRight - vUp, 1.f);
	Out[3].vPosition = mul(Out[3].vPosition, matVP);
	Out[3].vTexcoord = float2(0.0f, 1.0f);
    //Out[3].vProjPos = Out[3].vPosition;

    //float3 v2 = float3(Out[2].vPosition.xyz - Out[1].vPosition.xyz);
    //float3 v1 = float3(Out[0].vPosition.xyz - Out[1].vPosition.xyz);
    //float4 vNormal = float4(normalize(cross(v2, v1)), 0.f);
	
    //Out[0].vNormal = vNormal;
    //Out[1].vNormal = vNormal;
    //Out[2].vNormal = vNormal;
    //Out[3].vNormal = vNormal;
	
	OutStream.Append(Out[0]);
	OutStream.Append(Out[1]);
	OutStream.Append(Out[2]);
	OutStream.RestartStrip();

	OutStream.Append(Out[0]);
	OutStream.Append(Out[2]);
	OutStream.Append(Out[3]);
	OutStream.RestartStrip();
}

/* w나누기 연산. 진정한 투영변환. */
/* 뷰포트스페이스(윈도우좌표)로 위치를 변환한다. */
/* 래스터라이즈 : 정점에 둘러쌓인 픽셀의 정보를 생성한다. */
/* 픽셀정보는 정점정보에 기반한다. */

struct PS_IN
{
	float4 vPosition : SV_POSITION;
    //float4 vNormal : NORMAL;
	float2 vTexcoord : TEXCOORD0;
    //float4 vProjPos : TEXCOORD1;
};

/* 받아온 픽셀의 정보를 바탕으로 하여 화면에 그려질 픽셀의 최종적인 색을 결정하낟. */
struct PS_OUT
{
    float4 vGlow : SV_TARGET0;
    //float4 vNormal : SV_TARGET1;
    //float4 vDepth : SV_TARGET2;
};

/* 전달받은 픽셀의 정보를 이용하여 픽셀의 최종적인 색을 결정하자. */
PS_OUT PS_YELLOW_MAIN(PS_IN In) 
{
	PS_OUT Out = (PS_OUT)0;

    vector vDiffuse = g_Texture.Sample(PointSampler, In.vTexcoord);
   
    if (vDiffuse.r < 0.01f)
        discard;
	
    vector vYellow = vector(1.f, 1.f, 0.1f, 1.f);
    Out.vGlow = vYellow;

	return Out;
}

PS_OUT PS_RED_MAIN(PS_IN In)
{
    PS_OUT Out = (PS_OUT) 0;

    vector vDiffuse = g_Texture.Sample(PointSampler, In.vTexcoord);
   
    if (vDiffuse.r < 0.01f)
        discard;
	
    vector vRed = vector(1.f, 0.7f, 0.75f, 1.f);
    Out.vGlow = vRed;

    return Out;
}

PS_OUT PS_BLUE_MAIN(PS_IN In)
{
    PS_OUT Out = (PS_OUT) 0;

    vector vDiffuse = g_Texture.Sample(PointSampler, In.vTexcoord);
   
    if (vDiffuse.r < 0.01f)
        discard;
	
    vector vBlue = vector(0.35f, 0.75f, 1.2f, 1.f);
    Out.vGlow = vBlue;

    return Out;
}

technique11 DefaultTechnique
{
	/* */
	pass ParticleYellow
	{
		SetRasterizerState(RS_Default);
		SetDepthStencilState(DSS_Default, 0);
		SetBlendState(BS_Default, float4(0.f, 0.f, 0.f, 1.f), 0xffffffff);

		/* 여러 셰이더에 대해서 각각 어떤 버젼으로 빌드하고 어떤 함수를 호출하여 해당 셰이더가 구동되는지를 설정한다. */
		VertexShader = compile vs_5_0 VS_MAIN();
		GeometryShader = compile gs_5_0 GS_MAIN();
		HullShader = NULL;
		DomainShader = NULL;
        PixelShader = compile ps_5_0 PS_YELLOW_MAIN();
        ComputeShader = NULL;
    }

    pass ParticleRed
    {
        SetRasterizerState(RS_Default);
        SetDepthStencilState(DSS_Default, 0);
        SetBlendState(BS_Default, float4(0.f, 0.f, 0.f, 1.f), 0xffffffff);

		/* 여러 셰이더에 대해서 각각 어떤 버젼으로 빌드하고 어떤 함수를 호출하여 해당 셰이더가 구동되는지를 설정한다. */
        VertexShader = compile vs_5_0 VS_MAIN();
        GeometryShader = compile gs_5_0 GS_MAIN();
        HullShader = NULL;
        DomainShader = NULL;
        PixelShader = compile ps_5_0 PS_RED_MAIN();
        ComputeShader = NULL;

    }

    pass ParticleBlue
    {
        SetRasterizerState(RS_Default);
        SetDepthStencilState(DSS_Default, 0);
        SetBlendState(BS_Default, float4(0.f, 0.f, 0.f, 1.f), 0xffffffff);

		/* 여러 셰이더에 대해서 각각 어떤 버젼으로 빌드하고 어떤 함수를 호출하여 해당 셰이더가 구동되는지를 설정한다. */
        VertexShader = compile vs_5_0 VS_MAIN();
        GeometryShader = compile gs_5_0 GS_MAIN();
        HullShader = NULL;
        DomainShader = NULL;
        PixelShader = compile ps_5_0 PS_BLUE_MAIN();
        ComputeShader = NULL;

    }
}




