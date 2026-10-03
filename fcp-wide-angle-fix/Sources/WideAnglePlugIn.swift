//  Wide Angle Fix – FxPlug 4 filter for Final Cut Pro / Motion.
//  Squeezes (or widens) the left and right zones of a frame to correct
//  wide-angle edge stretching. See README.md for build steps.

import Foundation
import CoreMedia
import Metal

private enum ParamID: UInt32 {
    case linkSides = 1
    case center
    case leftGroup, leftZone, leftFeather, leftAmount
    case rightGroup, rightZone, rightFeather, rightAmount
    case fill
    case showZones
    case mix
}

private struct PluginState {
    var center: Float, mix: Float
    var zone: (Float, Float), feather: (Float, Float), amount: (Float, Float)
    var fill: Int32, showZones: Int32
}

@objc(WideAnglePlugIn)
class WideAnglePlugIn: NSObject, FxTileableEffect {
    let apiManager: PROAPIAccessing

    required init?(apiManager: PROAPIAccessing) {
        self.apiManager = apiManager
    }

    // MARK: Properties
    func properties(_ properties: AutoreleasingUnsafeMutablePointer<NSDictionary>?) throws {
        properties?.pointee = [
            kFxPropertyKey_MayRemapPixels: true,         // we sample away from the output pixel
            kFxPropertyKey_SupportsRect: true,
            kFxPropertyKey_ChangesOutputSize: false,
            kFxPropertyKey_NeedsFullBuffer: false,
            kFxPropertyKey_VariesWhenParamsAreStatic: false,
        ]
    }

    // MARK: Parameters
    func addParameters() throws {
        guard let p = apiManager.api(for: FxParameterCreationAPI_v5.self) as? FxParameterCreationAPI_v5 else {
            throw NSError(domain: FxPlugErrorDomain, code: kFxError_APIUnavailable, userInfo: nil)
        }
        let anim = FxParameterFlags(kFxParameterFlag_DEFAULT)
        let noAnim = FxParameterFlags(kFxParameterFlag_NOT_ANIMATABLE)

        p.addToggleButton(withName: "Link Left & Right", parameterID: ParamID.linkSides.rawValue,
                          defaultValue: true, parameterFlags: noAnim)
        p.addFloatSlider(withName: "Center Line (%)", parameterID: ParamID.center.rawValue,
                         defaultValue: 50, parameterMin: 20, parameterMax: 80,
                         sliderMin: 20, sliderMax: 80, delta: 0.1, parameterFlags: anim)

        for (side, ids) in [("Left", (ParamID.leftGroup, ParamID.leftZone, ParamID.leftFeather, ParamID.leftAmount)),
                            ("Right", (ParamID.rightGroup, ParamID.rightZone, ParamID.rightFeather, ParamID.rightAmount))] {
            p.startParameterSubGroup(side + " Side", parameterID: ids.0.rawValue, parameterFlags: noAnim)
            p.addFloatSlider(withName: "Zone Width (% of frame)", parameterID: ids.1.rawValue,
                             defaultValue: 33, parameterMin: 0, parameterMax: 50,
                             sliderMin: 0, sliderMax: 50, delta: 0.1, parameterFlags: anim)
            p.addFloatSlider(withName: "Feather (% of frame)", parameterID: ids.2.rawValue,
                             defaultValue: 10, parameterMin: 0, parameterMax: 50,
                             sliderMin: 0, sliderMax: 50, delta: 0.1, parameterFlags: anim)
            p.addFloatSlider(withName: "Squeeze (%)", parameterID: ids.3.rawValue,
                             defaultValue: 0, parameterMin: -50, parameterMax: 100,
                             sliderMin: -50, sliderMax: 100, delta: 0.1, parameterFlags: anim)
            p.endParameterSubGroup()
        }

        p.addToggleButton(withName: "Fill Frame (no crop, slight centre zoom)", parameterID: ParamID.fill.rawValue,
                          defaultValue: true, parameterFlags: noAnim)
        p.addToggleButton(withName: "Show Zones", parameterID: ParamID.showZones.rawValue,
                          defaultValue: false, parameterFlags: noAnim)
        p.addFloatSlider(withName: "Mix (%)", parameterID: ParamID.mix.rawValue,
                         defaultValue: 100, parameterMin: 0, parameterMax: 100,
                         sliderMin: 0, sliderMax: 100, delta: 0.1, parameterFlags: anim)
    }

    // When linked, editing a Left control copies it to Right.
    func parameterChanged(_ paramID: UInt32, at time: CMTime) throws {
        guard let get = apiManager.api(for: FxParameterRetrievalAPI_v6.self) as? FxParameterRetrievalAPI_v6,
              let set = apiManager.api(for: FxParameterSettingAPI_v5.self) as? FxParameterSettingAPI_v5 else { return }
        var linked: ObjCBool = false
        get.getBoolValue(&linked, fromParameter: ParamID.linkSides.rawValue, at: time)
        guard linked.boolValue else { return }
        let pairs: [(ParamID, ParamID)] = [(.leftZone, .rightZone), (.leftFeather, .rightFeather), (.leftAmount, .rightAmount)]
        for (l, r) in pairs where paramID == l.rawValue {
            var v = 0.0
            get.getFloatValue(&v, fromParameter: l.rawValue, at: time)
            set.setFloatValue(v, toParameter: r.rawValue, at: time)
        }
    }

    // MARK: Plug-in state (snapshot of parameter values for the render thread)
    func pluginState(_ pluginState: AutoreleasingUnsafeMutablePointer<NSData>?, at renderTime: CMTime, quality qualityLevel: UInt) throws {
        guard let get = apiManager.api(for: FxParameterRetrievalAPI_v6.self) as? FxParameterRetrievalAPI_v6 else {
            throw NSError(domain: FxPlugErrorDomain, code: kFxError_APIUnavailable, userInfo: nil)
        }
        func f(_ id: ParamID) -> Float { var v = 0.0; get.getFloatValue(&v, fromParameter: id.rawValue, at: renderTime); return Float(v) }
        func b(_ id: ParamID) -> Int32 { var v: ObjCBool = false; get.getBoolValue(&v, fromParameter: id.rawValue, at: renderTime); return v.boolValue ? 1 : 0 }

        var s = PluginState(
            center: f(.center) / 100, mix: f(.mix) / 100,
            zone: (f(.leftZone) / 100, f(.rightZone) / 100),
            feather: (f(.leftFeather) / 100, f(.rightFeather) / 100),
            amount: (f(.leftAmount) / 100, f(.rightAmount) / 100),
            fill: b(.fill), showZones: b(.showZones))
        pluginState?.pointee = NSData(bytes: &s, length: MemoryLayout<PluginState>.size)
    }

    // MARK: Tiling – horizontal remap needs the whole source row range, so ask for the full image.
    func destinationImageRect(_ destinationImageRect: UnsafeMutablePointer<FxRect>, sourceImages: [FxImageTile],
                              destinationImage: FxImageTile, pluginState: Data?, at renderTime: CMTime) throws {
        destinationImageRect.pointee = sourceImages[0].imagePixelBounds
    }

    func sourceTileRect(_ sourceTileRect: UnsafeMutablePointer<FxRect>, sourceImageIndex: UInt, sourceImages: [FxImageTile],
                        destinationTileRect: FxRect, destinationImage: FxImageTile, pluginState: Data?, at renderTime: CMTime) throws {
        sourceTileRect.pointee = sourceImages[0].imagePixelBounds
    }

    // MARK: Render
    func renderDestinationImage(_ destinationImage: FxImageTile, sourceImages: [FxImageTile],
                                pluginState: Data?, at renderTime: CMTime) throws {
        guard let data = pluginState, data.count == MemoryLayout<PluginState>.size, let srcTile = sourceImages.first else {
            throw NSError(domain: FxPlugErrorDomain, code: kFxError_InvalidParameter, userInfo: nil)
        }
        let st = data.withUnsafeBytes { $0.load(as: PluginState.self) }

        let cache = MetalDeviceCache.deviceCache
        guard let device = cache.device(with: sourceImages[0].deviceRegistryID),
              let queue = cache.commandQueue(withRegistryID: sourceImages[0].deviceRegistryID, pixelFormat: MTLPixelFormat.rgba16Float),
              let pipeline = cache.pipelineState(withRegistryID: sourceImages[0].deviceRegistryID, pixelFormat: MTLPixelFormat.rgba16Float),
              let dstTex = destinationImage.metalTexture(for: device),
              let srcTex = srcTile.metalTexture(for: device),
              let cmd = queue.makeCommandBuffer() else {
            throw NSError(domain: FxPlugErrorDomain, code: kFxError_InvalidParameter, userInfo: nil)
        }

        let imgB = sourceImages[0].imagePixelBounds
        let tileB = destinationImage.tilePixelBounds
        let w = Float(destinationImage.tilePixelBounds.right - tileB.left)
        let h = Float(destinationImage.tilePixelBounds.top - tileB.bottom)

        var params = WideAngleParams(
            imageSize: vector_float2(Float(imgB.right - imgB.left), Float(imgB.top - imgB.bottom)),
            tileOrigin: vector_float2(Float(tileB.left - imgB.left), Float(tileB.bottom - imgB.bottom)),
            center: st.center,
            zone: (st.zone.0, st.zone.1), feather: (st.feather.0, st.feather.1), amount: (st.amount.0, st.amount.1),
            fill: st.fill, showZones: st.showZones, mix: st.mix)

        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = dstTex
        pass.colorAttachments[0].loadAction = .dontCare
        pass.colorAttachments[0].storeAction = .store
        guard let enc = cmd.makeRenderCommandEncoder(descriptor: pass) else { return }

        enc.setViewport(MTLViewport(originX: 0, originY: 0, width: Double(w), height: Double(h), znear: -1, zfar: 1))
        enc.setRenderPipelineState(pipeline)

        let hw = w / 2, hh = h / 2
        var verts = [
            Vertex2D(position: vector_float4( hw, -hh, 0, 1), textureCoordinate: vector_float2(1, 0)),
            Vertex2D(position: vector_float4(-hw, -hh, 0, 1), textureCoordinate: vector_float2(0, 0)),
            Vertex2D(position: vector_float4(-hw,  hh, 0, 1), textureCoordinate: vector_float2(0, 1)),
            Vertex2D(position: vector_float4( hw, -hh, 0, 1), textureCoordinate: vector_float2(1, 0)),
            Vertex2D(position: vector_float4(-hw,  hh, 0, 1), textureCoordinate: vector_float2(0, 1)),
            Vertex2D(position: vector_float4( hw,  hh, 0, 1), textureCoordinate: vector_float2(1, 1)),
        ]
        var vp = vector_uint2(UInt32(w), UInt32(h))
        enc.setVertexBytes(&verts, length: MemoryLayout<Vertex2D>.stride * 6, index: Int(kVertexInputIndex_Vertices.rawValue))
        enc.setVertexBytes(&vp, length: MemoryLayout<vector_uint2>.size, index: Int(kVertexInputIndex_ViewportSize.rawValue))
        enc.setFragmentTexture(srcTex, index: Int(kTextureIndex_Source.rawValue))
        enc.setFragmentBytes(&params, length: MemoryLayout<WideAngleParams>.size, index: Int(kFragmentIndex_Params.rawValue))
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
        enc.endEncoding()
        cmd.commit()
        cmd.waitUntilScheduled()
        cache.returnCommandQueueToCache(queue: queue)
    }
}
