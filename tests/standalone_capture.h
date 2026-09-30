// 离屏 PNG 工具：WARP 渲染与 WIC 编码只操作测试纹理，不创建游戏窗口。
// 实际窗口与页面由本仓库生产源码绘制，不链接游戏挂钩。
#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <filesystem>
#include <iostream>
namespace standalone_visual {
inline void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
static void SavePng(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,const std::filesystem::path& path){
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging=nullptr;Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"staging texture");context->CopyResource(staging,texture);
    D3D11_MAPPED_SUBRESOURCE map{};Require(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)),"read pixels");
    IWICImagingFactory* factory=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* pngFrame=nullptr;
    Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"WIC factory");
    Require(SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"output stream");
    Require(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache)),"PNG encoder");
    Require(SUCCEEDED(encoder->CreateNewFrame(&pngFrame,nullptr))&&SUCCEEDED(pngFrame->Initialize(nullptr))&&SUCCEEDED(pngFrame->SetSize(desc.Width,desc.Height)),"PNG pngFrame");
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Require(SUCCEEDED(pngFrame->SetPixelFormat(&format))&&format==GUID_WICPixelFormat32bppBGRA,"pixel format");
    Require(SUCCEEDED(pngFrame->WritePixels(desc.Height,map.RowPitch,map.RowPitch*desc.Height,static_cast<BYTE*>(map.pData)))&&SUCCEEDED(pngFrame->Commit())&&SUCCEEDED(encoder->Commit()),"PNG write");
    pngFrame->Release();encoder->Release();stream->Release();factory->Release();context->Unmap(staging,0);staging->Release();
}
}
