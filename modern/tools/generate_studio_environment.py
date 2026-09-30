#!/usr/bin/env python3
"""Author a deterministic linear HDR studio cube and GGX roughness mip chain.

No downloaded image or runtime shader compiler is involved. Angular softboxes
and a dim studio background define incident radiance, including values above1.
Prefilter follows Karis, Real Shading in Unreal Engine4 (2013), pp4-5:
https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
MSTUDIO version1 is documented in StudioEnvironmentGPU.hpp. Cube orientation
is +X,-X,+Y,-Y,+Z,-Z with image rows from top to bottom (D3D/Vulkan sampling).
"""
import argparse
import hashlib
import math
import pathlib
import struct


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def normalize(v):
    scale = 1/math.sqrt(sum(x*x for x in v))
    return tuple(x*scale for x in v)


def smooth_edge(distance, half_extent, feather):
    t = max(0., min(1., (half_extent+feather-distance)/(2*feather)))
    return t*t*(3-2*t)


# Direction, rectangular angular extent, RGB radiance. Broad overhead key,
# narrow cool side strip, warm fill: authored luminaires reflected by metals.
def panel(direction, half_width, half_height, radiance):
    n = normalize(direction)
    right = normalize(cross((0.,1.,0.) if abs(n[1]) < .95 else (0.,0.,1.), n))
    up = cross(n, right)
    return n, right, up, half_width, half_height, radiance


PANELS = (
    panel((-.65, .75, -.5), .52, .26, (9., 9., 9.)),
    panel((.9, .25, .15), .12, .7, (5., 6., 8.)),
    panel((-.3, .15, .95), .42, .32, (3.5, 2.7, 1.8)),
)


def studio(direction):
    # Linear scene-referred radiance; no gamma transform or [0,1] clamp.
    y = max(0., direction[1])
    rgb = [.025+.075*y, .028+.078*y, .035+.085*y]
    for n, right, up, width, height, emission in PANELS:
        forward = sum(a*b for a,b in zip(n,direction))
        if forward <= 0.:
            continue
        x = abs(sum(a*b for a,b in zip(right,direction))/forward)
        z = abs(sum(a*b for a,b in zip(up,direction))/forward)
        weight = smooth_edge(x,width,.035)*smooth_edge(z,height,.035)
        for channel in range(3):
            rgb[channel] += emission[channel]*weight
    return rgb


def direction(face, u, v):
    return normalize(((1.,-v,-u), (-1.,-v,u), (u,1.,v),
                      (u,-1.,-v), (u,-v,1.), (-u,-v,-1.))[face])


def radical_inverse(bits):
    value, scale = 0., .5
    while bits:
        value += (bits & 1)*scale
        bits >>= 1
        scale *= .5
    return value


def ggx_samples(roughness, count):
    a2 = roughness**4
    result = []
    for sample in range(count):
        xi = radical_inverse(sample)
        cosine = math.sqrt((1-xi)/(1+(a2-1)*xi))
        sine = math.sqrt(max(0.,1-cosine*cosine))
        phi = 2*math.pi*sample/count
        result.append((math.cos(phi)*sine, math.sin(phi)*sine, cosine))
    return result


def prefilter(n, samples):
    t = normalize(cross((0.,0.,1.) if abs(n[2]) < .999 else (1.,0.,0.),n))
    b = cross(n,t)
    color, total = [0.,0.,0.], 0.
    for hx,hy,hz in samples:
        h = tuple(t[c]*hx+b[c]*hy+n[c]*hz for c in range(3))
        l = tuple(2*hz*h[c]-n[c] for c in range(3))  # V=N; V dot H=hz
        weight = max(0.,2*hz*hz-1)
        if weight:
            radiance = studio(l)
            for c in range(3):
                color[c] += radiance[c]*weight
            total += weight
    return [x/total for x in color] if total else studio(n)


def generate(width, sample_count):
    mip_count = width.bit_length()
    pixels = bytearray()
    for mip in range(mip_count):
        size = width >> mip
        roughness = mip/(mip_count-1) if mip_count > 1 else 0.
        samples = ggx_samples(roughness,sample_count) if mip else None
        for face in range(6):
            for row in range(size):
                for column in range(size):
                    n = direction(face,2*(column+.5)/size-1,2*(row+.5)/size-1)
                    rgb = prefilter(n,samples) if samples else studio(n)
                    assert all(math.isfinite(x) and 0 <= x <= 65504 for x in rgb)
                    pixels.extend(struct.pack('<4e',*rgb,1.))
        print(f'mip {mip}: {size}x{size} six faces, roughness {roughness:.6f}',flush=True)
    header = struct.pack('<8s6I',b'MSTUDIO\0',1,width,mip_count,6,1,len(pixels))
    assert len(header)+len(pixels) <= 2*1024*1024
    return header+pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--width',type=int,default=64)
    parser.add_argument('--samples',type=int,default=256)
    args = parser.parse_args()
    if args.width < 1 or args.width > 128 or args.width & (args.width-1):
        parser.error('width must be a power of two from1 to128')
    if args.samples < 16 or args.samples > 4096:
        parser.error('samples must be from16 to4096')
    data = generate(args.width,args.samples)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(data)
    print(f'{args.output}: {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()}')


if __name__ == '__main__':
    main()
