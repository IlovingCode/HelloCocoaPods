//
//  GameEngine.cpp
//  hello-triangle
//
//  Created by Nguyen Cong Thien on 25/04/2022.
//

#include "GameEngine.hpp"

// TODO: Filament public headers in the 1.8.1 release use DEBUG as a C++ identifier, but Xcode
// defines DEBUG=1. So, we simply undefine it here. This will be fixed in the next release.
#undef DEBUG

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// These are all C++ headers, so make sure the type of this file is Objective-C++ source.
#include <ktxreader/Ktx2Reader.h>
#include <ktxreader/Ktx1Reader.h>
#include <image/Ktx1Bundle.h>

#include <filament/Engine.h>
#include <filament/SwapChain.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Scene.h>
#include <filament/Viewport.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/TransformManager.h>
#include <filament/LightManager.h>
#include <filament/TextureSampler.h>
#include <filament/Texture.h>
#include <filament/Skybox.h>
#include <filament/IndirectLight.h>

#include <utils/Entity.h>
#include <utils/Path.h>
#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/materials/uberarchive.h>
#include <gltfio/math.h>
#include <gltfio/Animator.h>

// #include <btBulletDynamicsCommon.h>

#include <JavaScriptCore/JavaScript.h>
#include "Object_C_Interface.h"

#include "MyExtension.h"
#ifdef ANDROID
#include <android/asset_manager.h>
#else
#include <iostream>
#include <fstream>
#include <sstream>
#endif

#ifndef JSMACRO
#define JSMACRO
#define JSCALLBACK(name) JSValueRef name(JSContextRef ctx, JSObjectRef function, JSObjectRef object, size_t argumentCount, const JSValueRef arguments[], JSValueRef *exception)
#endif

using namespace std;
using namespace filament;
using namespace gltfio;
using namespace utils;
using namespace spine;

Renderer *renderer;
View *view;
SwapChain *swapChain;
AssetLoader *assetLoader;
void *nativeHandle;

// btDiscreteDynamicsWorld *dynamicsWorld;

#ifdef ANDROID
AAssetManager *assetManager = nullptr;
#else
Path assets;
#endif

JSGlobalContextRef globalContext;
double current_time;

GameEngine::~GameEngine()
{
    Engine *engine = renderer->getEngine();
    engine->destroy(view->getScene());

    engine->destroy(view);
    engine->destroy(renderer);
    engine->destroy(swapChain);
    engine->destroy(&engine);

    assetLoader->getMaterialProvider().destroyMaterials();
    delete &(assetLoader->getMaterialProvider());
    AssetLoader::destroy(&assetLoader);

    //    JSContextGroupRef contextGroup = JSContextGetGroup(globalContext);
    JSGlobalContextRelease(globalContext);
    //    JSContextGroupRelease(contextGroup);

    // if (dynamicsWorld)
    // {
    //     // cleanup in the reverse order of creation/initialization
    //     // remove the rigidbodies from the dynamics world and delete them
    //     for (int i = dynamicsWorld->getNumCollisionObjects() - 1; i >= 0; i--)
    //     {
    //         btCollisionObject *obj = dynamicsWorld->getCollisionObjectArray()[i];
    //         btRigidBody *body = btRigidBody::upcast(obj);
    //         if (body && body->getMotionState())
    //         {
    //             delete body->getMotionState();
    //         }
    //         dynamicsWorld->removeCollisionObject(obj);
    //         delete obj;
    //     }

    //     btDispatcher *dispatcher = dynamicsWorld->getDispatcher();
    //     btBroadphaseInterface *overlappingPairCache = dynamicsWorld->getBroadphase();
    //     btConstraintSolver *solver = dynamicsWorld->getConstraintSolver();

    //     // delete dynamics world
    //     delete dynamicsWorld;

    //     // delete solver
    //     delete solver;

    //     // delete broadphase
    //     delete overlappingPairCache;

    //     // delete dispatcher
    //     delete dispatcher;

    //     dynamicsWorld = nullptr;
    // }
}

string JSValueToStdString(JSContextRef context, JSValueRef jsValue)
{
    JSValueRef error = nullptr;
    JSStringRef jsString = JSValueToStringCopy(context, jsValue, &error);
    size_t maxBufferSize = JSStringGetMaximumUTF8CStringSize(jsString);
    char *utf8Buffer = new char[maxBufferSize];
    size_t bytesWritten = JSStringGetUTF8CString(jsString, utf8Buffer, maxBufferSize);

    string utf_string = string(utf8Buffer, bytesWritten - 1); // the last byte is a null \0 which std::string doesn't need.
    JSStringRelease(jsString);
    delete[] utf8Buffer;
    return utf_string.compare("null") == 0 ? "" : utf_string;
}

const uint8_t *loadFile(const char *filename, size_t *size)
{
    uint8_t *data = nullptr;

#ifdef ANDROID
    AAsset *asset = AAssetManager_open(assetManager, filename, 0);
    data = (uint8_t *)AAsset_getBuffer(asset);
    *size = AAsset_getLength(asset);
//    AAsset_close(asset);
#else
    ifstream file(assets + filename, ios::binary | ios::ate);
    *size = file.tellg();
    data = new uint8_t[*size];
    file.seekg(0, ios::beg);
    file.read((char *)data, *size);
    file.close();
#endif

    return data;
}

Texture *loadTexture(const char *filename)
{
    size_t size = 0;
    const uint8_t *data = loadFile(filename, &size);

    ktxreader::Ktx2Reader reader(*renderer->getEngine());

    // Uncompressed formats are lower priority, so they get added last.
    reader.requestFormat(Texture::InternalFormat::SRGB8_A8);
    reader.requestFormat(Texture::InternalFormat::RGBA8);

    Texture *texture = reader.load(data, size, ktxreader::Ktx2Reader::TransferFunction::sRGB);

    delete[] data;

    return texture;
}

void unloadTexture(Texture *texture)
{
    renderer->getEngine()->destroy(texture);
}

math::float3 eulerAngles(math::quatf q)
{
    math::quatf nq = normalize(q);
    return math::float3{
        // roll (x-axis rotation)
        (atan2(2.0f * (nq.y * nq.z + nq.w * nq.x),
               nq.w * nq.w - nq.x * nq.x - nq.y * nq.y + nq.z * nq.z)),
        // pitch (y-axis rotation)
        (asin(-2.0f * (nq.x * nq.z - nq.w * nq.y))),
        // yaw (z-axis rotation)
        (atan2(2.0f * (nq.x * nq.y + nq.w * nq.z),
               nq.w * nq.w + nq.x * nq.x - nq.y * nq.y - nq.z * nq.z))};
}

JSCALLBACK(log)
{
    string str = "";
    for (uint8_t i = 0; i < argumentCount; ++i)
    {
        str += JSValueToStdString(ctx, arguments[i]) + ' ';
    }
    LOGI("%s\n", str.c_str());

    return nullptr;
}

JSCALLBACK(beginScene)
{
    Scene *scene = renderer->getEngine()->createScene();

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, scene, sizeof(scene), nullptr, nullptr, nullptr);
}

JSCALLBACK(playAnimation)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *buffer = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Animator *animator = static_cast<FilamentInstance *>(buffer)->getAnimator();

    size_t id = JSValueToNumber(ctx, arguments[1], nullptr);
    float time = JSValueToNumber(ctx, arguments[2], nullptr);

    animator->applyAnimation(id, time);
    animator->updateBoneMatrices();

    return arguments[0];
}

JSCALLBACK(setEnvironment)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *buffer = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Scene *scene = static_cast<Scene *>(buffer);
    Engine *engine = renderer->getEngine();

    size_t size = 0;
    const uint8_t *data = nullptr;

    {
        data = loadFile(JSValueToStdString(ctx, arguments[1]).c_str(), &size);

        image::Ktx1Bundle *bundle =
            new image::Ktx1Bundle(data, static_cast<uint32_t>(size));

        Texture *texture = ktxreader::Ktx1Reader::createTexture(engine, bundle, true);
        Skybox *skybox = Skybox::Builder().environment(texture).build(*engine);
        scene->setSkybox(skybox);

        delete[] data;
    }
    {
        data = loadFile(JSValueToStdString(ctx, arguments[2]).c_str(), &size);

        image::Ktx1Bundle *bundle =
            new image::Ktx1Bundle(data, static_cast<uint32_t>(size));
        Texture *texture = ktxreader::Ktx1Reader::createTexture(engine, bundle, true);

        math::float3 harmonics[9];
        bundle->getSphericalHarmonics(harmonics);
        IndirectLight *indirectLight = IndirectLight::Builder()
                                           .reflections(texture)
                                           .irradiance(3, harmonics)
                                           .intensity(JSValueToNumber(ctx, arguments[3], nullptr))
                                           .build(*engine);

        scene->setIndirectLight(indirectLight);

        delete[] data;
    }

    return arguments[0];
}

JSCALLBACK(addModel)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *buffer = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    FilamentAsset *primary = static_cast<FilamentAsset *>(buffer);

    array = JSValueToObject(ctx, arguments[1], nullptr);
    buffer = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Scene *scene = static_cast<Scene *>(buffer);

    JSObjectRef data = JSValueToObject(ctx, arguments[2], nullptr);

    JSStringRef name = JSStringCreateWithUTF8CString("nodes");
    JSObjectRef nodes = JSValueToObject(ctx, JSObjectGetProperty(ctx, data, name, nullptr), nullptr);
    JSStringRelease(name);

    name = JSStringCreateWithUTF8CString("relations");
    JSObjectRef relations = JSValueToObject(ctx, JSObjectGetProperty(ctx, data, name, nullptr), nullptr);
    JSStringRelease(name);

    name = JSStringCreateWithUTF8CString("animations");
    JSObjectRef animations = JSValueToObject(ctx, JSObjectGetProperty(ctx, data, name, nullptr), nullptr);
    JSStringRelease(name);

    name = JSStringCreateWithUTF8CString("animationDurations");
    JSObjectRef animationDurations = JSValueToObject(ctx, JSObjectGetProperty(ctx, data, name, nullptr), nullptr);
    JSStringRelease(name);

    size_t index = JSValueToNumber(ctx, arguments[3], nullptr);
    FilamentInstance *bundle = primary->getInstance();
    if(index >= primary->getAssetInstanceCount()) bundle = assetLoader->createInstance(primary);
    
    Engine *engine = renderer->getEngine();
    const Entity *entities = bundle->getEntities();
    size_t count = bundle->getEntityCount();
    scene->addEntities(entities, count);
    TransformManager &tcm = engine->getTransformManager();

    Entity e = bundle->getRoot();
    name = JSStringCreateWithUTF8CString(primary->getSceneName(0));
    JSValueRef id = JSValueMakeNumber(ctx, Entity::smuggle(e));
    JSObjectSetProperty(ctx, nodes, name, id, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(name);

    for (size_t i = 0; i < count; ++i)
    {
        e = entities[i];
        name = JSStringCreateWithUTF8CString(primary->getName(e));
        id = JSValueMakeNumber(ctx, Entity::smuggle(e));
        JSObjectSetProperty(ctx, nodes, name, id, kJSPropertyAttributeNone, nullptr);

        e = tcm.getParent(tcm.getInstance(e));
        id = JSValueMakeNumber(ctx, Entity::smuggle(e));
        JSObjectSetProperty(ctx, relations, name, id, kJSPropertyAttributeNone, nullptr);

        JSStringRelease(name);
    }

    if (primary->getCameraEntityCount() > 0)
    {
        Camera *camera = engine->getCameraComponent(primary->getCameraEntities()[0]);
        name = JSStringCreateWithUTF8CString("fov");
        JSValueRef fov = JSValueMakeNumber(ctx, camera->getFieldOfViewInDegrees(Camera::Fov::HORIZONTAL));
        JSObjectSetProperty(ctx, data, name, fov, kJSPropertyAttributeNone, nullptr);
        JSStringRelease(name);
    }

    if (primary->getLightEntityCount() > 0)
    {
        LightManager &lightMgr = engine->getLightManager();

        name = JSStringCreateWithUTF8CString("lightIntensity");
        JSValueRef intensity = JSValueMakeNumber(ctx, lightMgr.getIntensity(lightMgr.getInstance(primary->getLightEntities()[0])));
        JSObjectSetProperty(ctx, data, name, intensity, kJSPropertyAttributeNone, nullptr);
        JSStringRelease(name);
    }

    Animator *animator = bundle->getAnimator();
    count = animator->getAnimationCount();
    for (size_t i = 0; i < count; ++i)
    {
        name = JSStringCreateWithUTF8CString(animator->getAnimationName(i));
        id = JSValueMakeNumber(ctx, i);
        JSValueRef duration = JSValueMakeNumber(ctx, animator->getAnimationDuration(i));
        JSObjectSetProperty(ctx, animations, name, id, kJSPropertyAttributeNone, nullptr);
        JSObjectSetProperty(ctx, animationDurations, name, duration, kJSPropertyAttributeNone, nullptr);

        JSStringRelease(name);
    }

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, bundle, sizeof(bundle), nullptr, nullptr, nullptr);
}

JSCALLBACK(loadModel)
{
    Engine *engine = renderer->getEngine();

    static ResourceLoader *resourceLoader = nullptr;

    if (resourceLoader == nullptr)
    {
        MaterialProvider *materialProvider = createUbershaderProvider(engine,
                                                                      UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);

        EntityManager &em = EntityManager::get();
        NameComponentManager *ncm = new NameComponentManager(em);
        assetLoader = AssetLoader::create({engine, materialProvider, ncm, &em});

        resourceLoader = new ResourceLoader({.engine = engine,
                                             .normalizeSkinningWeights = false});
        TextureProvider *stbDecoder = createStbProvider(engine);
        TextureProvider *ktxDecoder = createKtx2Provider(engine);

        resourceLoader->addTextureProvider("image/png", stbDecoder);
        resourceLoader->addTextureProvider("image/jpeg", stbDecoder);
        resourceLoader->addTextureProvider("image/ktx2", ktxDecoder);
    }

    // Load the glTF file.
    size_t size = 0;
    const uint8_t *data = loadFile(JSValueToStdString(ctx, arguments[0]).c_str(), &size);

    FilamentAsset *bunble = assetLoader->createAsset(data, static_cast<uint32_t>(size));
    resourceLoader->loadResources(bunble);

    delete[] data;

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, bunble, sizeof(bunble), nullptr, nullptr, nullptr);
}

JSCALLBACK(createEntity)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Scene *scene = static_cast<Scene *>(data);

    Entity e = EntityManager::get().create();
    scene->addEntity(e);

    int32_t id = JSValueToNumber(ctx, arguments[1], nullptr);
    if (id >= 0)
    {
        auto &tcm = renderer->getEngine()->getTransformManager();
        Entity parent = Entity::import(id);
        tcm.create(e, tcm.getInstance(parent));
    }

    return JSValueMakeNumber(ctx, Entity::smuggle(e));
}

JSCALLBACK(destroyEntity)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Scene *scene = static_cast<Scene *>(data);

    uint32_t id = JSValueToNumber(ctx, arguments[1], nullptr);
    Entity e = Entity::import(id);
    scene->remove(e);
    renderer->getEngine()->destroy(e);
    EntityManager::get().destroy(e);

    return arguments[0];
}

JSCALLBACK(getLocalTransform)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    //    size_t count = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    void *buffer = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);
    float *d = static_cast<float *>(buffer);

    auto &tcm = renderer->getEngine()->getTransformManager();
    Entity parent = Entity::import(d[0]);

    const math::mat4f world = tcm.getTransform(tcm.getInstance(parent));

    math::float3 translation, scale, rotation;
    math::quatf quaternion;

    gltfio::decomposeMatrix(world, &translation, &quaternion, &scale);

    rotation = eulerAngles(quaternion);

    d[1] = translation.x;
    d[2] = translation.y;
    d[3] = translation.z;

    d[4] = scale.x;
    d[5] = scale.y;
    d[6] = scale.z;

    d[7] = rotation.x;
    d[8] = rotation.y;
    d[9] = rotation.z;

    return arguments[0];
}

JSCALLBACK(getWorldTransform)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    //    size_t count = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    void *buffer = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);
    float *d = static_cast<float *>(buffer);

    auto &tcm = renderer->getEngine()->getTransformManager();
    Entity parent = Entity::import(d[0]);

    const math::mat4f world = tcm.getWorldTransform(tcm.getInstance(parent));

    math::float3 translation, scale, rotation;
    math::quatf quaternion;

    gltfio::decomposeMatrix(world, &translation, &quaternion, &scale);

    rotation = eulerAngles(quaternion);

    d[1] = translation.x;
    d[2] = translation.y;
    d[3] = translation.z;

    d[4] = scale.x;
    d[5] = scale.y;
    d[6] = scale.z;

    d[7] = rotation.x;
    d[8] = rotation.y;
    d[9] = rotation.z;

    return arguments[0];
}

JSCALLBACK(loadImage)
{
    Texture *texture = loadTexture(JSValueToStdString(ctx, arguments[0]).c_str());

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, texture, sizeof(texture), nullptr, nullptr, nullptr);
}

JSCALLBACK(loadSpine)
{
    // Load the json file.
    string filename = JSValueToStdString(ctx, arguments[0]);

    TextureLoader *textureLoader = new MyTextureLoader(loadTexture, unloadTexture);

    // Load the texture atlas
    Atlas *atlas = new Atlas((filename + ".atlas").c_str(), textureLoader);
    if (atlas->getPages().size() == 0)
    {
        cout << "Failed to load atlas\n";
        delete atlas;
        return nullptr;
    }

    // Load the skeleton data
    SkeletonJson json(atlas);
    SkeletonData *skeletonData = json.readSkeletonDataFile((filename + ".json").c_str());
    if (!skeletonData)
    {
        cout << "Failed to load skeleton data\n";
        delete atlas;
        return nullptr;
    }

    AnimationStateData *animationData = new AnimationStateData(skeletonData);
    float mix = JSValueToNumber(ctx, arguments[2], nullptr);
    animationData->setDefaultMix(mix);

    JSObjectRef data = JSValueToObject(ctx, arguments[1], nullptr);

    JSStringRef name = JSStringCreateWithUTF8CString("skeletonData");
    JSObjectRef value = JSObjectMakeArrayBufferWithBytesNoCopy(ctx, skeletonData, sizeof(skeletonData), nullptr, nullptr, nullptr);
    JSObjectSetProperty(ctx, data, name, value, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(name);

    name = JSStringCreateWithUTF8CString("animationData");
    value = JSObjectMakeArrayBufferWithBytesNoCopy(ctx, animationData, sizeof(animationData), nullptr, nullptr, nullptr);
    JSObjectSetProperty(ctx, data, name, value, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(name);

    return arguments[1];
}

SpineExtension *spine::getDefaultExtension()
{
    return new MyExtension(loadFile);
}

IndexBuffer *getIndexBuffer()
{
    static IndexBuffer *ib;

    if (ib == nullptr)
    {
#define MAXSTRINGLENGTH 200
        const int LENGTH = 78 + MAXSTRINGLENGTH * 6;
        static uint16_t TEMPLATE[LENGTH] = {
            // 9-slices 16 vertices (0, 54)
            0, 1, 2, 3, 2, 1,
            1, 4, 3, 6, 3, 4,
            4, 5, 6, 7, 6, 5,
            10, 11, 0, 1, 0, 11,
            11, 14, 1, 4, 1, 14,
            14, 15, 4, 5, 4, 15,
            8, 9, 10, 11, 10, 9,
            9, 12, 11, 14, 11, 12,
            12, 13, 14, 15, 14, 13,
            // radial   17 vertices (54, 24)
            1, 2, 8, 3, 4, 8,
            5, 9, 8, 10, 15, 8,
            16, 14, 8, 13, 12, 8,
            11, 7, 8, 6, 0, 8,
            // simple   4 vertices  (78, MAXSTRINGLENGTH * 6)
        };

        uint16_t count = 0, id = 78;
        for (int i = 0; i < MAXSTRINGLENGTH; ++i)
        {
            TEMPLATE[id + 0] = count + 0;
            TEMPLATE[id + 1] = count + 1;
            TEMPLATE[id + 2] = count + 2;
            TEMPLATE[id + 3] = count + 3;
            TEMPLATE[id + 4] = count + 2;
            TEMPLATE[id + 5] = count + 1;

            id += 6;
            count += 4;
        }

        Engine *engine = renderer->getEngine();
        ib = IndexBuffer::Builder()
                 .indexCount(LENGTH)
                 .bufferType(IndexBuffer::IndexType::USHORT)
                 .build(*engine);
        ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(TEMPLATE, LENGTH * 2, nullptr));
    }

    return ib;
}

JSCALLBACK(updateRenderer)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    VertexBuffer *vb = static_cast<VertexBuffer *>(data);

    array = JSValueToObject(ctx, arguments[1], nullptr);
    size_t vc = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    data = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);

    Engine *engine = renderer->getEngine();
    vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(data, vc * 4, nullptr));

    if (argumentCount > 2)
    {
        size_t count = JSValueToNumber(ctx, arguments[2], nullptr);
        uint32_t id = JSValueToNumber(ctx, arguments[3], nullptr);
        auto &rm = engine->getRenderableManager();
        auto instance = rm.getInstance(Entity::import(id));

        rm.setGeometryAt(instance, 0, RenderableManager::PrimitiveType::TRIANGLES, vb, getIndexBuffer(), 78, count / 16 * 6);
    }

    return arguments[0];
}

JSCALLBACK(updateMaterial)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);

    auto &rm = renderer->getEngine()->getRenderableManager();
    auto instance = rm.getInstance(Entity::import(id));
    MaterialInstance *material = rm.getMaterialInstanceAt(instance, 0);

    if (argumentCount > 3)
    {
        double red = JSValueToNumber(ctx, arguments[1], nullptr);
        double green = JSValueToNumber(ctx, arguments[2], nullptr);
        double blue = JSValueToNumber(ctx, arguments[3], nullptr);
        double alpha = JSValueToNumber(ctx, arguments[4], nullptr);

        material->setParameter("baseColor", RgbaType::LINEAR, math::float4{red, green, blue, alpha});
    }
    else if (argumentCount > 2)
    {
        bool isVisible = JSValueToBoolean(ctx, arguments[1]);
        uint8_t mask = JSValueToNumber(ctx, arguments[2], nullptr);

        rm.setLayerMask(instance, mask, isVisible ? mask : 0x0);
    }
    else
    {
        JSObjectRef array = JSValueToObject(ctx, arguments[1], nullptr);
        void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
        Texture *texture = static_cast<Texture *>(data);

        material->setParameter("texture", texture, TextureSampler(TextureSampler::MagFilter::LINEAR));
    }

    return arguments[0];
}

// JSCALLBACK(updateScissor) {
//     JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
//     size_t count = JSObjectGetTypedArrayLength(ctx, array, nullptr);
//     void* buffer = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);
//     uint32_t* d = static_cast<uint32_t*>(buffer);
//
//     uint32_t left = 0, bottom = 0, width = 0, height = 0;
//     if(argumentCount > 1) {
//         left = JSValueToNumber(ctx, arguments[1], nullptr);
//         bottom = JSValueToNumber(ctx, arguments[2], nullptr);
//         width = JSValueToNumber(ctx, arguments[3], nullptr);
//         height = JSValueToNumber(ctx, arguments[4], nullptr);
//     }
//
//     auto& rm = engine->getRenderableManager();
//     for (uint32_t i = 0; i < count; ++i) {
//         MaterialInstance* material = rm.getMaterialInstanceAt(rm.getInstance(Entity::import(d[i])), 0);
//
//         if(argumentCount > 1) material->setScissor(left, bottom, width, height);
//         else material->unsetScissor();
//     }
//
//     return arguments[0];
// }

JSCALLBACK(playAudio)
{
    string filename = JSValueToStdString(ctx, arguments[0]);
#ifdef ANDROID
    playNativeAudio(nativeHandle, filename.c_str());
#else
    playNativeAudio(nativeHandle, ("assets/" + filename).c_str());
#endif

    return arguments[0];
}

JSCALLBACK(renderText)
{
    size_t size = 0;
    const uint8_t *data = loadFile(JSValueToStdString(ctx, arguments[0]).c_str(), &size);

    string word = JSValueToStdString(ctx, arguments[1]);

    JSObjectRef array = JSValueToObject(ctx, arguments[2], nullptr);
    //    size_t count = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    void *buffer = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);
    short *d = static_cast<short *>(buffer);

    /* prepare font */
    stbtt_fontinfo info;
    stbtt_InitFont(&info, data, 0);

    delete[] data;

    int b_w = JSValueToNumber(ctx, arguments[3], nullptr); /* bitmap width */
    int b_h = JSValueToNumber(ctx, arguments[4], nullptr); /* bitmap height */
    int l_h = JSValueToNumber(ctx, arguments[5], nullptr); /* line height */

    /* create a bitmap for the phrase */
    unsigned char *bitmap = (unsigned char *)calloc(b_w * b_h, sizeof(unsigned char));

    /* calculate font scaling */
    float scale = stbtt_ScaleForPixelHeight(&info, l_h);

    int x = 0;
    int y = 0;

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    ascent = roundf(ascent * scale);
    descent = roundf(descent * scale);
    lineGap = roundf(lineGap * scale);
    int min = descent;

    d[0] = ascent;
    d[1] = descent;
    d[2] = lineGap;

    for (int i = 0; i < word.length(); ++i)
    {
        /* how wide is this character */
        int ax;
        int lsb;
        stbtt_GetCodepointHMetrics(&info, word[i], &ax, &lsb);
        ax = roundf(ax * scale);
        lsb = roundf(lsb * scale);
        /* (Note that each Codepoint call has an alternative Glyph version which caches the work required to lookup the character word[i].) */

        /* get bounding box for character (may be offset to account for chars that dip above or below the line) */
        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(&info, word[i], scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);

        if (c_y2 > min)
            min = c_y2;

        /* compute y (different characters have different heights) */
        // int y = ascent + c_y1;
        if (x + ax > b_w)
        {
            x = 0;
            y += ascent + min / 2;
            min = descent;
        }

        /* render character (stride and offset is important here) */
        int byteOffset = x + lsb + (y + ascent + c_y1) * b_w;
        stbtt_MakeCodepointBitmap(&info, bitmap + byteOffset, c_x2 - c_x1, c_y2 - c_y1, b_w, scale, scale, word[i]);

        byteOffset = i * 7 + 3;
        d[byteOffset + 0] = ax;
        d[byteOffset + 1] = lsb;
        d[byteOffset + 2] = c_y1;
        d[byteOffset + 3] = c_x2 - c_x1;
        d[byteOffset + 4] = c_y2 - c_y1;
        d[byteOffset + 5] = x + lsb;
        d[byteOffset + 6] = y + ascent + c_y1;

        /* advance x */
        x += ax;

        /* add kerning */
        //        int kern;
        //        kern = stbtt_GetCodepointKernAdvance(&info, word[i], word[i + 1]);
        //        x += roundf(kern * scale);
    }

    Engine *engine = renderer->getEngine();
    Texture *texture = Texture::Builder()
                           .format(Texture::InternalFormat::R8)
                           .width(b_w)
                           .height(b_h)
                           .build(*engine);

    Texture::PixelBufferDescriptor::Callback freeCallback = [](void *buf, size_t, void *userdata)
    {
        free(buf);
    };

    Texture::PixelBufferDescriptor pbd(
        bitmap, b_w * b_h,
        Texture::PixelBufferDescriptor::PixelDataFormat::R,
        Texture::PixelBufferDescriptor::PixelDataType::UBYTE,
        freeCallback,
        nullptr);

    texture->setImage(*engine, 0, move(pbd));
    getIndexBuffer();

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, texture, sizeof(texture), nullptr, nullptr, nullptr);
}

JSCALLBACK(addText)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
    Entity entity = Entity::import(id);

    JSObjectRef array = JSValueToObject(ctx, arguments[1], nullptr);
    size_t vc = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    void *VERTICES = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);

    Engine *engine = renderer->getEngine();
    static Material *mat;

    if (mat == nullptr)
    {
        // This file is compiled via the matc tool. See the "Run Script" build phase.
        constexpr uint8_t BAKED_TEXT_PACKAGE[] = {
#ifdef ANDROID
#include "bakedText.inc"
#else
#include "bakedText_ios.inc"
#endif
        };

        mat = Material::Builder()
                  .package((void *)BAKED_TEXT_PACKAGE, sizeof(BAKED_TEXT_PACKAGE))
                  .build(*engine);

        mat->setDefaultParameter("baseColor", RgbaType::LINEAR, math::float4{1, 1, 1, 1});
    }

    //    IndexBuffer* ib = IndexBuffer::Builder()
    //        .indexCount((uint32_t)ic)
    //        .bufferType(IndexBuffer::IndexType::USHORT)
    //        .build(*engine);
    //    ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(INDICES, ic * 2, nullptr));

    VertexBuffer *vb = VertexBuffer::Builder()
                           .vertexCount((uint32_t)vc / 4)
                           .bufferCount(1)
                           .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, 0, 16)
                           .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 8, 16)
                           .build(*engine);
    vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(VERTICES, vc * 4, nullptr));

    uint8_t maskValue = JSValueToNumber(ctx, arguments[3], nullptr);
    auto matInstance = mat->createInstance();

    array = JSValueToObject(ctx, arguments[2], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Texture *texture = static_cast<Texture *>(data);
    matInstance->setParameter("texture", texture, TextureSampler(TextureSampler::MagFilter::LINEAR));
    //    matInstance->setDepthWrite(false);
    //    matInstance->setDepthCulling(false);
    matInstance->setStencilCompareFunction(MaterialInstance::StencilCompareFunc::LE);
    matInstance->setStencilReferenceValue(maskValue);

    RenderableManager::Builder(1)
        .boundingBox({{-1, -1, -1}, {1, 1, 1}})
        .material(0, matInstance)
        //        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
        .culling(true)
        .receiveShadows(false)
        .castShadows(false)
        .build(*engine, entity);

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, vb, sizeof(vb), nullptr, nullptr, nullptr);
}

MaterialInstance *getMaterial(Engine *engine, bool isMask)
{
    static Material *mat, *mask;

    if (mat == nullptr)
    {
        // This file is compiled via the matc tool. See the "Run Script" build phase.
        constexpr uint8_t BAKED_COLOR_PACKAGE[] = {
#ifdef ANDROID
#include "bakedColor.inc"
#else
#include "bakedColor_ios.inc"
#endif
        };
        constexpr uint8_t BAKED_MASK_PACKAGE[] = {
#ifdef ANDROID
#include "bakedMask.inc"
#else
#include "bakedMask_ios.inc"
#endif
        };

        mat = Material::Builder()
                  .package((void *)BAKED_COLOR_PACKAGE, sizeof(BAKED_COLOR_PACKAGE))
                  .build(*engine);

        mask = Material::Builder()
                   .package((void *)BAKED_MASK_PACKAGE, sizeof(BAKED_MASK_PACKAGE))
                   .build(*engine);
    }

    return (isMask ? mask : mat)->createInstance();
}

JSCALLBACK(playSpine)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    AnimationState *animator = static_cast<AnimationState *>(data);

    string name = JSValueToStdString(ctx, arguments[1]);
    bool loop = JSValueIsBoolean(ctx, arguments[2]);
    float delay = JSValueToNumber(ctx, arguments[3], nullptr);

    TrackEntry *track = nullptr;
    if (delay < 0)
        track = animator->setAnimation(0, name.c_str(), loop);
    else
        track = animator->addAnimation(0, name.c_str(), loop, delay);

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, track, sizeof(track), nullptr, nullptr, nullptr);
}

JSCALLBACK(updateSpine)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    VertexBuffer *vb = static_cast<VertexBuffer *>(data);

    array = JSValueToObject(ctx, arguments[1], nullptr);
    data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    AnimationState *animator = static_cast<AnimationState *>(data);

    array = JSValueToObject(ctx, arguments[2], nullptr);
    data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Skeleton *skeleton = static_cast<Skeleton *>(data);

    array = JSValueToObject(ctx, arguments[3], nullptr);
    data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    float *vertices = static_cast<float *>(data);

    float dt = JSValueToNumber(ctx, arguments[4], nullptr);

    animator->update(dt);
    animator->apply(*skeleton);
    skeleton->updateWorldTransform();

    Vector<Slot *> slots = skeleton->getSlots();
    size_t vertCount = 0;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        Slot *slot = slots[i];
        Attachment *attachment = slot->getAttachment();
        if (!attachment)
            continue;

        if (attachment->getRTTI().isExactly(RegionAttachment::rtti))
        {
            RegionAttachment *region = static_cast<RegionAttachment *>(attachment);

            region->computeWorldVertices(*slot, vertices, vertCount * 4, 4);
            vertCount += 4;
        }
        else
        {
            MeshAttachment *mesh = static_cast<MeshAttachment *>(attachment);
            size_t size = mesh->getWorldVerticesLength();

            mesh->computeWorldVertices(*slot, 0, size, vertices, vertCount * 4, 4);
            vertCount += size >> 1;
        }
    }

    Engine *engine = renderer->getEngine();
    vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(vertices, vertCount * 16, nullptr));

    return arguments[0];
}

JSCALLBACK(addSpine)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
    Entity entity = Entity::import(id);

    JSObjectRef array = JSValueToObject(ctx, arguments[1], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    SkeletonData *skeletonData = static_cast<SkeletonData *>(data);

    array = JSValueToObject(ctx, arguments[2], nullptr);
    data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    AnimationStateData *animationData = static_cast<AnimationStateData *>(data);

    Skeleton *skeleton = new Skeleton(skeletonData);
    AnimationState *animator = new AnimationState(animationData);

    Vector<Slot *> slots = skeleton->getSlots();
    size_t vertCount = 0, trisCount = 0;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        Slot *slot = slots[i];
        Attachment *attachment = slot->getAttachment();
        if (!attachment)
            continue;

        if (attachment->getRTTI().isExactly(RegionAttachment::rtti))
        {
            vertCount += 4;
            trisCount += 6;
        }
        else
        {
            MeshAttachment *mesh = static_cast<MeshAttachment *>(attachment);
            vertCount += mesh->getWorldVerticesLength() >> 1;
            trisCount += mesh->getTriangles().size();
        }
    }

    float *vertices = new float[vertCount * 4];
    uint16_t *indices = new uint16_t[trisCount];
    Texture *texture = nullptr;
    float *uvs = nullptr;
    vertCount = 0;
    trisCount = 0;

    for (size_t i = 0; i < slots.size(); ++i)
    {
        Slot *slot = slots[i];
        Attachment *attachment = slot->getAttachment();
        if (!attachment)
            continue;

        if (attachment->getRTTI().isExactly(RegionAttachment::rtti))
        {
            RegionAttachment *region = static_cast<RegionAttachment *>(attachment);
            texture = (Texture *)((AtlasRegion *)region->getRegion())->page->texture;
            uvs = region->getUVs().buffer();

            for (size_t j = 0; j < 4; ++j)
            {
                vertices[(vertCount + j) * 4 + 2] = uvs[j * 2 + 0];
                vertices[(vertCount + j) * 4 + 3] = 1 - uvs[j * 2 + 1];
            }

            indices[trisCount + 0] = vertCount + 0;
            indices[trisCount + 1] = vertCount + 1;
            indices[trisCount + 2] = vertCount + 2;
            indices[trisCount + 3] = vertCount + 2;
            indices[trisCount + 4] = vertCount + 3;
            indices[trisCount + 5] = vertCount + 0;

            vertCount += 4;
            trisCount += 6;
        }
        else
        {
            MeshAttachment *mesh = static_cast<MeshAttachment *>(attachment);
            texture = (Texture *)((AtlasRegion *)mesh->getRegion())->page->texture;
            Vector<unsigned short> tris = mesh->getTriangles();
            size_t size = mesh->getWorldVerticesLength() >> 1;
            uvs = mesh->getUVs().buffer();

            for (size_t j = 0; j < tris.size(); ++j)
                indices[trisCount + j] = vertCount + tris[j];

            for (size_t j = 0; j < size; ++j)
            {
                vertices[(vertCount + j) * 4 + 2] = uvs[j * 2 + 0];
                vertices[(vertCount + j) * 4 + 3] = 1 - uvs[j * 2 + 1];
            }

            vertCount += size;
            trisCount += tris.size();
        }
    }

    Engine *engine = renderer->getEngine();
    VertexBuffer *vb = VertexBuffer::Builder()
                           .vertexCount((uint32_t)vertCount)
                           .bufferCount(1)
                           .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, 0, 16)
                           .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 8, 16)
                           .build(*engine);

    vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(vertices, vertCount * 16, nullptr));

    IndexBuffer *ib = IndexBuffer::Builder()
                          .indexCount((uint32_t)trisCount)
                          .bufferType(IndexBuffer::IndexType::USHORT)
                          .build(*engine);
    ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(indices, trisCount * 2, nullptr));

    auto matInstance = getMaterial(engine, false);
    matInstance->setParameter("texture", texture, TextureSampler(TextureSampler::MagFilter::LINEAR));
    matInstance->setStencilReferenceValue(0);
    matInstance->setStencilCompareFunction(MaterialInstance::StencilCompareFunc::LE);

    RenderableManager::Builder(1)
        .boundingBox({{-1, -1, -1}, {1, 1, 1}})
        .material(0, matInstance)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib, 0, trisCount)
        .culling(false)
        .receiveShadows(false)
        .castShadows(false)
        .build(*engine, entity);

    array = JSValueToObject(ctx, arguments[3], nullptr);

    JSStringRef name = JSStringCreateWithUTF8CString("animator");
    JSValueRef value = JSObjectMakeArrayBufferWithBytesNoCopy(ctx, animator, sizeof(animator), nullptr, nullptr, nullptr);
    JSObjectSetProperty(ctx, array, name, value, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(name);

    name = JSStringCreateWithUTF8CString("vertices");
    value = JSObjectMakeArrayBufferWithBytesNoCopy(ctx, vertices, vertCount * 16, nullptr, nullptr, nullptr);
    JSObjectSetProperty(ctx, array, name, value, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(name);

    name = JSStringCreateWithUTF8CString("skeleton");
    value = JSObjectMakeArrayBufferWithBytesNoCopy(ctx, skeleton, sizeof(skeleton), nullptr, nullptr, nullptr);
    JSObjectSetProperty(ctx, array, name, value, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(name);

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, vb, sizeof(vb), nullptr, nullptr, nullptr);
}

JSCALLBACK(addRenderer)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
    Entity entity = Entity::import(id);

    JSObjectRef array = JSValueToObject(ctx, arguments[1], nullptr);
    size_t count = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    void *VERTICES = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);

    Engine *engine = renderer->getEngine();

    VertexBuffer *vb = VertexBuffer::Builder()
                           .vertexCount((uint32_t)count / 4)
                           .bufferCount(1)
                           .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, 0, 16)
                           .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 8, 16)
                           .build(*engine);
    vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(VERTICES, count * 4, nullptr));

    bool isMask = JSValueToBoolean(ctx, arguments[3]);
    uint8_t maskValue = JSValueToNumber(ctx, arguments[4], nullptr);
    auto matInstance = getMaterial(engine, isMask);

    array = JSValueToObject(ctx, arguments[2], nullptr);
    void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
    Texture *texture = static_cast<Texture *>(data);
    matInstance->setParameter("texture", texture, TextureSampler(TextureSampler::MagFilter::LINEAR));
    //    matInstance->setDepthWrite(false);
    //    matInstance->setDepthCulling(false);
    matInstance->setStencilReferenceValue(maskValue);
    if (isMask)
    {
        matInstance->setStencilOpDepthStencilPass(MaterialInstance::StencilOperation::REPLACE);
        matInstance->setStencilWrite(true);
    }
    else
    {
        matInstance->setStencilCompareFunction(MaterialInstance::StencilCompareFunc::LE);
    }

    int offset = 0;
    if (count > 64)
    {
        offset = 54;
        count = 24;
    }
    else if (count > 16)
    {
        offset = 0;
        count = 54;
    }
    else
    {
        offset = 78;
        count = 6;
    }

    array = JSValueToObject(ctx, arguments[5], nullptr);
    data = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);
    float *bbox = static_cast<float *>(data);

    RenderableManager::Builder(1)
        .boundingBox({{bbox[0], bbox[1], bbox[2]}, {bbox[3], bbox[4], bbox[5]}})
        .material(0, matInstance)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, getIndexBuffer(), offset, count)
        .culling(true)
        .receiveShadows(false)
        .castShadows(false)
        .build(*engine, entity);

    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, vb, sizeof(vb), nullptr, nullptr, nullptr);
}

JSCALLBACK(updateTransforms)
{
    JSObjectRef array = JSValueToObject(ctx, arguments[0], nullptr);
    size_t count = JSObjectGetTypedArrayLength(ctx, array, nullptr);
    void *buffer = JSObjectGetTypedArrayBytesPtr(ctx, array, nullptr);
    float *d = static_cast<float *>(buffer);

    const uint8_t STRIKE = 10;

    auto &tcm = renderer->getEngine()->getTransformManager();
    tcm.openLocalTransformTransaction();

    for (uint32_t i = 0; i < count; i += STRIKE)
    {
        uint32_t id = d[i];

        math::float3 pos{d[i + 1], d[i + 2], d[i + 3]};
        math::float3 rot{d[i + 4], d[i + 5], d[i + 6]};
        math::float3 scl{d[i + 7], d[i + 8], d[i + 9]};

        tcm.setTransform(tcm.getInstance(Entity::import(id)),
                         math::mat4f::translation(pos) *
                             math::mat4f::eulerZYX(rot.x, rot.y, rot.z) *
                             math::mat4f::scaling(scl));
    }

    tcm.commitLocalTransformTransaction();

    return arguments[0];
}

JSCALLBACK(addCamera)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
    Entity entity = Entity::import(id);

    Camera *camera = renderer->getEngine()->createCamera(entity);
    return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, camera, sizeof(camera), nullptr, nullptr, nullptr);
}

JSCALLBACK(updateLight)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
    Entity entity = Entity::import(id);

    LightManager &lightMgr = renderer->getEngine()->getLightManager();
    auto instance = lightMgr.getInstance(entity);

    TransformManager &tcm = renderer->getEngine()->getTransformManager();
    const math::mat4f transform = tcm.getWorldTransform(tcm.getInstance(entity));

    float intensity = JSValueToNumber(ctx, arguments[1], nullptr);
    lightMgr.setDirection(instance, {transform[0][2], transform[1][2], -transform[2][2]});
    lightMgr.setIntensity(instance, intensity);

    return nullptr;
}

// JSCALLBACK(addRigidBody)
// {
//     btScalar mass(JSValueToNumber(ctx, arguments[0], nullptr));
//     size_t type = (size_t)JSValueToNumber(ctx, arguments[1], nullptr);
//     btVector3 data;

//     for(size_t i = 2; i < argumentCount; ++i) {
//         data[i - 2] = JSValueToNumber(ctx, arguments[i], nullptr);
//     }

//     btCollisionShape *shape = nullptr;

//     switch (type)
//     {
//     case 0:
//         shape = new btSphereShape(data[0]);
//         break;
//     case 1:
//         shape = new btCapsuleShapeX(data[0], data[1]);
//         break;
//     case 2:
//         shape = new btCapsuleShape(data[0], data[1]);
//         break;
//     case 3:
//         shape = new btCapsuleShapeZ(data[0], data[1]);
//         break;
//     case 4:
//         shape = new btBoxShape(data);
//         break;
//     case 5:
//         shape = new btCylinderShapeX(data);
//         break;
//     case 6:
//         shape = new btCylinderShape(data);
//         break;
//     case 7:
//         shape = new btCylinderShapeZ(data);
//         break;
//     default:
//         break;
//     }

//     btTransform transform;
//     transform.setIdentity();

//     // rigidbody is dynamic if and only if mass is non zero, otherwise static
//     bool isDynamic = (mass != 0.f);

//     btVector3 localInertia(0, 0, 0);
//     if (isDynamic && shape)
//         shape->calculateLocalInertia(mass, localInertia);

//     // using motionstate is optional, it provides interpolation capabilities, and only synchronizes 'active' objects
//     btDefaultMotionState *myMotionState = new btDefaultMotionState(transform);
//     btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, myMotionState, shape, localInertia);
//     btRigidBody *body = new btRigidBody(rbInfo);

//     // add the body to the dynamics world
//     dynamicsWorld->addRigidBody(body);

//     return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, body, sizeof(body), nullptr, nullptr, nullptr);
// }

// JSCALLBACK(beginPhysics)
// {
//     ///-----initialization_start-----

//     /// collision configuration contains default setup for memory, collision setup. Advanced users can create their own configuration.
//     btDefaultCollisionConfiguration *collisionConfiguration = new btDefaultCollisionConfiguration();

//     /// use the default collision dispatcher. For parallel processing you can use a diffent dispatcher (see Extras/BulletMultiThreaded)
//     btCollisionDispatcher *dispatcher = new btCollisionDispatcher(collisionConfiguration);

//     /// btDbvtBroadphase is a good general purpose broadphase. You can also try out btAxis3Sweep.
//     btBroadphaseInterface *overlappingPairCache = new btDbvtBroadphase();

//     /// the default constraint solver. For parallel processing you can use a different solver (see Extras/BulletMultiThreaded)
//     btSequentialImpulseConstraintSolver *solver = new btSequentialImpulseConstraintSolver;

//     dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);

//     dynamicsWorld->setGravity(btVector3(0, -9.8, 0));

//     ///-----initialization_end-----
//     return JSObjectMakeArrayBufferWithBytesNoCopy(ctx, collisionConfiguration, sizeof(collisionConfiguration), nullptr, nullptr, nullptr);
// }

// JSCALLBACK(updateRigidBody)
// {
//     uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
//     Entity entity = Entity::import(id);
    
//     JSObjectRef array = JSValueToObject(ctx, arguments[1], nullptr);
//     void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
//     btRigidBody *body = static_cast<btRigidBody *>(data);

//     auto &tcm = renderer->getEngine()->getTransformManager();

//     const math::mat4f world = tcm.getTransform(tcm.getInstance(entity));
//     btTransform transform;
//     transform.setFromOpenGLMatrix(world.asArray());

//     //    math::float3 translation, scale, rotation;
//     //    math::quatf quaternion;

//     //    gltfio::decomposeMatrix(world, &translation, &quaternion, &scale);

//     body->getMotionState()->setWorldTransform(transform);

//     return arguments[0];
// }

JSCALLBACK(updateCamera)
{
    uint32_t id = JSValueToNumber(ctx, arguments[0], nullptr);
    Entity entity = Entity::import(id);

    auto camera = renderer->getEngine()->getCameraComponent(entity);

    if (argumentCount < 4)
    {
        const double width = JSValueToNumber(ctx, arguments[1], nullptr);
        const double height = JSValueToNumber(ctx, arguments[2], nullptr);

        const double right = width * .5;
        const double top = height * .5;
        const double left = -right;
        const double bottom = -top;
        const double near = 0.0;
        const double far = 2.0;

        camera->setProjection(Camera::Projection::ORTHO, left, right, bottom, top, near, far);
    }
    else
    {
        //        const double fov = JSValueToNumber(ctx, arguments[1], nullptr);
        const double aspect = JSValueToNumber(ctx, arguments[2], nullptr);
        const double scale = JSValueToNumber(ctx, arguments[3], nullptr);

        camera->setScaling({scale, scale * aspect});
    }

    return arguments[0];
}

JSCALLBACK(render)
{
    view->setBlendMode(filament::BlendMode::OPAQUE);
    view->setPostProcessingEnabled(true);

    if (renderer->beginFrame(swapChain))
    {
        for (size_t i = 0; i < argumentCount - 1; i += 2)
        {
            JSObjectRef array = JSValueToObject(ctx, arguments[i], nullptr);
            void *data = JSObjectGetArrayBufferBytesPtr(ctx, array, nullptr);
            Scene *scene = static_cast<Scene *>(data);

            uint32_t id = JSValueToNumber(ctx, arguments[i + 1], nullptr);
            Entity entity = Entity::import(id);
            Camera *camera = renderer->getEngine()->getCameraComponent(entity);

            view->setScene(scene);
            view->setCamera(camera);
            renderer->render(view);

            view->setBlendMode(filament::BlendMode::TRANSLUCENT);
            view->setPostProcessingEnabled(true);
        }
        renderer->endFrame();
    }

    return nullptr;
}

void registerNativeFunction(const char *name, JSObjectCallAsFunctionCallback callback, JSObjectRef thisObject)
{
    JSStringRef funcName = JSStringCreateWithUTF8CString(name);
    JSObjectRef func = JSObjectMakeFunctionWithCallback(globalContext, funcName, callback);
    JSObjectSetProperty(globalContext, thisObject, funcName, func, kJSPropertyAttributeNone, nullptr);
    JSStringRelease(funcName);
}

JSObjectRef getScriptFunction(const char *name, JSObjectRef thisObject)
{
    JSStringRef funcName = JSStringCreateWithUTF8CString(name);
    JSValueRef func = JSObjectGetProperty(globalContext, thisObject, funcName, nullptr);
    JSStringRelease(funcName);

    return JSValueToObject(globalContext, func, nullptr);
}

void GameEngine::input(float x, float y, uint8_t state)
{
    static JSStringRef xStr, yStr, stateStr;
    static JSObjectRef input;

    auto viewport = view->getViewport();

    x = x - viewport.width * .5f;
    y = viewport.height * .5f - y;

    if (input == nullptr)
    {
        JSStringRef inputStr = JSStringCreateWithUTF8CString("input");
        JSObjectRef globalObject = JSContextGetGlobalObject(globalContext);
        xStr = JSStringCreateWithUTF8CString("x");
        yStr = JSStringCreateWithUTF8CString("y");
        stateStr = JSStringCreateWithUTF8CString("state");
        input = JSValueToObject(globalContext, JSObjectGetProperty(globalContext, globalObject, inputStr, nullptr), nullptr);

        JSStringRelease(inputStr);
    }

    JSObjectSetProperty(globalContext, input, xStr, JSValueMakeNumber(globalContext, x), kJSPropertyAttributeNone, nullptr);
    JSObjectSetProperty(globalContext, input, yStr, JSValueMakeNumber(globalContext, y), kJSPropertyAttributeNone, nullptr);
    JSObjectSetProperty(globalContext, input, stateStr, JSValueMakeNumber(globalContext, state), kJSPropertyAttributeNone, nullptr);
}

void GameEngine::setNativeHandle(void *handle)
{
#ifdef ANDROID
    Engine *engine = renderer->getEngine();
    if (handle)
        swapChain = engine->createSwapChain(handle);
    else
    {
        engine->destroy(swapChain);
        swapChain = nullptr;
        engine->flushAndWait();
    }
#else
    nativeHandle = handle;
#endif
}

GameEngine::GameEngine(void *nativeWindow, double now)
{
    Engine *engine = Engine::create(
#ifdef ANDROID
        Engine::Backend::OPENGL
#else
        Engine::Backend::METAL
#endif
    );
    swapChain = engine->createSwapChain(nativeWindow);
    renderer = engine->createRenderer();
    view = engine->createView();
    current_time = now;

    //    view->setBlendMode(BlendMode::TRANSLUCENT);
    //    view->setPostProcessingEnabled(true);
    view->setStencilBufferEnabled(true);

    view->setAntiAliasing(AntiAliasing::NONE);
    view->setDithering(Dithering::NONE);
    view->setScreenSpaceRefractionEnabled(false);
    view->setShadowingEnabled(false);

    //    auto cg = ColorGrading::Builder()
    //        .contrast(1.f)
    //        .build(*engine);
    //    view->setColorGrading(cg);

    renderer->setClearOptions({.clearColor = {0.1, 0.125, 0.25, 1.0}, .clear = false});

    globalContext = JSGlobalContextCreate(nullptr);
    JSObjectRef globalObject = JSContextGetGlobalObject(globalContext);

    registerNativeFunction("beginScene", beginScene, globalObject);
    registerNativeFunction("addCamera", addCamera, globalObject);
    registerNativeFunction("log", log, globalObject);
    registerNativeFunction("createEntity", createEntity, globalObject);
    registerNativeFunction("destroyEntity", destroyEntity, globalObject);
    registerNativeFunction("addRenderer", addRenderer, globalObject);
    registerNativeFunction("updateTransforms", updateTransforms, globalObject);
    registerNativeFunction("updateCamera", updateCamera, globalObject);
    registerNativeFunction("updateRenderer", updateRenderer, globalObject);
    registerNativeFunction("updateMaterial", updateMaterial, globalObject);
    registerNativeFunction("getWorldTransform", getWorldTransform, globalObject);
    registerNativeFunction("getLocalTransform", getLocalTransform, globalObject);
    registerNativeFunction("loadImage", loadImage, globalObject);
    registerNativeFunction("addText", addText, globalObject);
    registerNativeFunction("renderText", renderText, globalObject);
    registerNativeFunction("render", render, globalObject);
    registerNativeFunction("playAudio", playAudio, globalObject);
    registerNativeFunction("loadModel", loadModel, globalObject);
    registerNativeFunction("loadSpine", loadSpine, globalObject);
    registerNativeFunction("addSpine", addSpine, globalObject);
    registerNativeFunction("playSpine", playSpine, globalObject);
    registerNativeFunction("updateSpine", updateSpine, globalObject);
    registerNativeFunction("addModel", addModel, globalObject);
    registerNativeFunction("updateLight", updateLight, globalObject);
    registerNativeFunction("playAnimation", playAnimation, globalObject);
    registerNativeFunction("setEnvironment", setEnvironment, globalObject);
    // registerNativeFunction("beginPhysics", beginPhysics, globalObject);
    // registerNativeFunction("addRigidBody", addRigidBody, globalObject);
    // registerNativeFunction("updateRigidBody", updateRigidBody, globalObject);

#ifdef ANDROID
    AAsset *asset = AAssetManager_open(assetManager, "bundle.js", 0);
    string source = string((const char *)AAsset_getBuffer(asset), AAsset_getLength(asset) - 1);
    AAsset_close(asset);

    asset = AAssetManager_open(assetManager, "bundle_game.js", 0);
    source += string((const char *)AAsset_getBuffer(asset), AAsset_getLength(asset) - 1);
    AAsset_close(asset);

    asset = AAssetManager_open(assetManager, "bundle_ui.js", 0);
    source += string((const char *)AAsset_getBuffer(asset), AAsset_getLength(asset) - 1);
    AAsset_close(asset);
#else
    assets = Path::getCurrentExecutable().getParent() + "assets/";
    ostringstream buffer;

    ifstream file(assets + "bundle.js");
    buffer << file.rdbuf();
    file.close();

    ifstream file_game(assets + "bundle_game.js");
    buffer << file_game.rdbuf();
    file_game.close();

    ifstream file_ui(assets + "bundle_ui.js");
    buffer << file_ui.rdbuf();
    file_ui.close();

    string source = buffer.str();
#endif

    JSValueRef exception = nullptr;
    JSStringRef script = JSStringCreateWithUTF8CString(source.c_str());
    JSEvaluateScript(globalContext, script, nullptr, nullptr, 0, &exception);
    if (exception)
        LOGI("Thien %s", JSValueToStdString(globalContext, exception).c_str());

    JSStringRelease(script);
}

void GameEngine::update(double now)
{
    double delta = now - current_time;
    current_time = now;

    JSValueRef dt = JSValueMakeNumber(globalContext, delta);
    static JSObjectRef updateLoop;

    if (updateLoop == nullptr)
    {
        JSObjectRef globalObject = JSContextGetGlobalObject(globalContext);
        updateLoop = getScriptFunction("update", globalObject);
    }

    JSValueRef exception = nullptr;
    JSObjectCallAsFunction(globalContext, updateLoop, nullptr, 1, &dt, &exception);
    if (exception)
        LOGI("Thien %s", JSValueToStdString(globalContext, exception).c_str());
}

void GameEngine::resize(uint16_t width, uint16_t height)
{
    view->setViewport({0, 0, width, height});

    const JSValueRef args[2]{
        JSValueMakeNumber(globalContext, width),
        JSValueMakeNumber(globalContext, height)};

    static JSObjectRef resizeView;

    if (resizeView == nullptr)
    {
        JSObjectRef globalObject = JSContextGetGlobalObject(globalContext);
        resizeView = getScriptFunction("resizeView", globalObject);
    }

    JSObjectCallAsFunction(globalContext, resizeView, nullptr, 2, args, nullptr);
}
