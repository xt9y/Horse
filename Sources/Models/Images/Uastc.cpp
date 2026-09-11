#include "Models/Images/Uastc.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Models::Images::Uastc {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint32_t bits(const std::uint8_t *block, unsigned int offset, unsigned int count)
{
    std::uint32_t value = 0u;
    for (unsigned int bit = 0u; bit < count; ++bit) {
        const unsigned int position = offset + bit;
        value |= static_cast<std::uint32_t>((block[position >> 3u] >> (position & 7u)) & 1u) << bit;
    }
    return value;
}

struct Mode {
    std::uint8_t code;
    std::uint8_t code_bits;
    std::uint8_t components;
    std::uint8_t subsets;
    std::uint8_t planes;
    std::uint8_t endpoint_range;
    std::uint8_t weight_bits;
    std::uint8_t pattern_kind;
    std::uint8_t pattern_offset;
    std::uint8_t pattern_bits;
    std::int8_t component_offset;
    std::uint8_t endpoint_offset;
    std::uint8_t weight_offset;
};

constexpr std::array<Mode, 19> modes {{
    {0x01,4,3,1,1,19,4,0,0,0,-1,19,65},
    {0x35,6,3,1,1,20,2,0,0,0,-1,21,69},
    {0x1d,5,3,2,1, 8,3,1,20,5,-1,25,73},
    {0x03,5,3,3,1, 7,2,2,20,4,-1,24,89},
    {0x13,5,3,2,1,12,2,1,20,5,-1,25,89},
    {0x0b,5,3,1,1,20,3,0,0,0,-1,20,68},
    {0x1b,5,3,1,2,18,2,0,0,0,20,22,66},
    {0x07,5,3,2,1,12,2,3,20,5,-1,25,89},
    {0x17,5,4,0,0, 0,0,0,0,0,-1,0,0},
    {0x0f,5,4,2,1, 8,2,1,28,5,-1,33,97},
    {0x02,3,4,1,1,13,4,0,0,0,-1,20,65},
    {0x00,2,4,1,2,13,2,0,0,0,19,21,66},
    {0x06,3,4,1,1,19,3,0,0,0,-1,20,81},
    {0x1f,5,4,1,2,20,1,0,0,0,28,30,94},
    {0x0d,5,4,1,1,20,2,0,0,0,-1,28,92},
    {0x05,7,2,1,1,20,4,0,0,0,-1,30,62},
    {0x15,6,2,2,1,20,2,1,29,5,-1,34,98},
    {0x25,6,2,1,2,20,2,0,0,0,-1,29,61},
    {0x09,4,3,1,1,11,5,0,0,0,-1,19,49},
}};

using Pattern = std::array<std::uint8_t, 16>;
constexpr std::array<Pattern, 30> patterns2 {{
    Pattern{0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1}, Pattern{0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1},
    Pattern{1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0}, Pattern{0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
    Pattern{1,1,1,1,1,1,1,0,1,1,1,0,1,1,0,0}, Pattern{0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1},
    Pattern{1,1,1,0,1,1,0,0,1,0,0,0,0,0,0,0}, Pattern{1,1,1,1,1,1,1,0,1,1,0,0,1,0,0,0},
    Pattern{0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1}, Pattern{1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0},
    Pattern{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1}, Pattern{1,1,1,1,1,1,1,1,1,1,1,0,1,0,0,0},
    Pattern{1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0}, Pattern{1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0},
    Pattern{0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1}, Pattern{1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0},
    Pattern{1,0,0,0,1,1,1,0,1,1,1,1,1,1,1,1}, Pattern{1,1,1,1,1,1,1,1,0,1,1,1,0,0,0,1},
    Pattern{0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0}, Pattern{0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
    Pattern{0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0}, Pattern{1,1,1,1,1,1,1,1,0,1,1,1,0,0,1,1},
    Pattern{1,0,0,0,1,1,0,0,1,1,0,0,1,1,1,0}, Pattern{0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0},
    Pattern{1,1,1,1,0,1,1,1,0,1,1,1,0,0,1,1}, Pattern{0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0},
    Pattern{1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1}, Pattern{1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
    Pattern{1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0}, Pattern{1,0,0,1,0,0,1,1,0,1,1,0,1,1,0,0},
}};

constexpr std::array<std::array<std::uint8_t,2>,30> anchors2 {{
    {{0,2}},{{0,3}},{{1,0}},{{0,3}},{{7,0}},{{0,2}},{{3,0}},{{7,0}},{{0,11}},{{2,0}},
    {{0,7}},{{11,0}},{{3,0}},{{8,0}},{{0,4}},{{12,0}},{{1,0}},{{8,0}},{{0,1}},{{0,2}},
    {{0,4}},{{8,0}},{{1,0}},{{0,2}},{{4,0}},{{0,1}},{{4,0}},{{1,0}},{{4,0}},{{1,0}},
}};

constexpr std::array<Pattern, 11> patterns3 {{
    Pattern{0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2}, Pattern{1,1,1,1,1,1,1,1,0,0,0,0,2,2,2,2},
    Pattern{1,1,1,1,0,0,0,0,0,0,0,0,2,2,2,2}, Pattern{1,1,1,1,2,2,2,2,0,0,0,0,0,0,0,0},
    Pattern{1,1,2,0,1,1,2,0,1,1,2,0,1,1,2,0}, Pattern{0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2},
    Pattern{0,2,1,1,0,2,1,1,0,2,1,1,0,2,1,1}, Pattern{2,0,0,0,2,0,0,0,2,1,1,1,2,1,1,1},
    Pattern{2,0,1,2,2,0,1,2,2,0,1,2,2,0,1,2}, Pattern{1,1,1,1,0,0,0,0,2,2,2,2,1,1,1,1},
    Pattern{0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2},
}};

constexpr std::array<std::array<std::uint8_t,3>,11> anchors3 {{
    {{0,8,10}},{{8,0,12}},{{4,0,12}},{{8,0,4}},{{3,0,2}},{{0,1,3}},
    {{0,2,1}},{{1,9,0}},{{1,2,0}},{{4,0,8}},{{0,6,2}},
}};

constexpr std::array<Pattern, 19> patterns7 {{
    Pattern{0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0}, Pattern{0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0},
    Pattern{1,1,0,0,1,1,0,0,1,0,0,0,0,0,0,0}, Pattern{0,0,0,0,0,0,0,1,0,0,1,1,0,0,1,1},
    Pattern{1,1,1,1,1,1,1,1,0,0,0,0,1,1,1,1}, Pattern{0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0},
    Pattern{0,0,0,1,0,0,1,1,1,1,1,1,1,1,1,1}, Pattern{0,1,1,1,0,0,1,1,0,0,1,1,0,0,1,1},
    Pattern{1,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0}, Pattern{0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0},
    Pattern{0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,0}, Pattern{1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0},
    Pattern{0,1,1,1,0,0,1,1,0,0,0,0,0,0,0,0}, Pattern{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1},
    Pattern{1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,0}, Pattern{1,1,0,0,1,1,0,0,1,1,0,0,1,0,0,0},
    Pattern{1,1,1,1,1,1,1,1,1,0,0,0,1,0,0,0}, Pattern{0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,0},
    Pattern{1,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0},
}};

constexpr std::array<std::array<std::uint8_t,2>,19> anchors7 {{
    {{0,4}},{{0,2}},{{2,0}},{{0,7}},{{8,0}},{{0,1}},{{0,3}},{{0,1}},{{2,0}},{{0,1}},
    {{0,8}},{{2,0}},{{0,1}},{{0,7}},{{12,0}},{{2,0}},{{9,0}},{{0,2}},{{4,0}},
}};

const Pattern *patternFor(const Mode& mode, std::uint32_t index, std::array<std::uint8_t,3> *anchors, std::string *error)
{
    if (!anchors) return nullptr;
    *anchors = {{0,0,0}};
    if (mode.pattern_kind == 0u) return nullptr;
    if (mode.pattern_kind == 1u) {
        if (index >= patterns2.size()) { fail(error, "invalid UASTC 2-subset partition index"); return nullptr; }
        (*anchors)[0] = anchors2[index][0]; (*anchors)[1] = anchors2[index][1];
        return &patterns2[index];
    }
    if (mode.pattern_kind == 2u) {
        if (index >= patterns3.size()) { fail(error, "invalid UASTC 3-subset partition index"); return nullptr; }
        *anchors = anchors3[index];
        return &patterns3[index];
    }
    if (index >= patterns7.size()) { fail(error, "invalid UASTC mode-7 partition index"); return nullptr; }
    (*anchors)[0] = anchors7[index][0]; (*anchors)[1] = anchors7[index][1];
    return &patterns7[index];
}

bool endpointInfo(std::uint8_t range, unsigned int *value_bits, unsigned int *radix, std::string *error)
{
    if (!value_bits || !radix) return fail(error, "invalid UASTC endpoint range output");
    switch (range) {
        case 7: *value_bits=2; *radix=3; return true;
        case 8: *value_bits=4; *radix=1; return true;
        case 11:*value_bits=5; *radix=1; return true;
        case 12:*value_bits=3; *radix=5; return true;
        case 13:*value_bits=4; *radix=3; return true;
        case 18:*value_bits=5; *radix=5; return true;
        case 19:*value_bits=6; *radix=3; return true;
        case 20:*value_bits=8; *radix=1; return true;
        default:return fail(error, "unsupported UASTC endpoint range");
    }
}

std::uint8_t unquantize(std::uint8_t range, std::uint32_t value)
{
    unsigned int n = 0u;
    unsigned int radix = 1u;
    if (!endpointInfo(range, &n, &radix, nullptr)) return 0u;
    if (radix == 1u) {
        const std::uint32_t levels = std::uint32_t{1u} << n;
        return static_cast<std::uint8_t>((value * 255u + (levels - 2u) / 2u) / (levels - 1u));
    }
    const std::uint32_t low = value & ((std::uint32_t{1u} << n) - 1u);
    const std::uint32_t digit = value >> n;
    const std::uint32_t a = low & 1u;
    const std::uint32_t A = a != 0u ? 0x1ffu : 0u;
    std::uint32_t B = 0u;
    std::uint32_t C = 0u;
    if (range == 7u) {
        const std::uint32_t b=(low>>1u)&1u; B=b*278u; C=93u;
    } else if (range == 12u) {
        const std::uint32_t b=(low>>1u)&1u,c=(low>>2u)&1u; B=c*261u+b*130u; C=26u;
    } else if (range == 13u) {
        const std::uint32_t b=(low>>1u)&1u,c=(low>>2u)&1u,d=(low>>3u)&1u; B=d*260u+c*130u+b*65u; C=22u;
    } else if (range == 18u) {
        const std::uint32_t b=(low>>1u)&1u,c=(low>>2u)&1u,d=(low>>3u)&1u,e=(low>>4u)&1u; B=e*257u+d*128u+c*64u+b*32u; C=6u;
    } else {
        const std::uint32_t b=(low>>1u)&1u,c=(low>>2u)&1u,d=(low>>3u)&1u,e=(low>>4u)&1u,f=(low>>5u)&1u; B=f*257u+e*128u+d*64u+c*32u+b*16u; C=5u;
    }
    const std::uint32_t result = ((digit*C+B)^A);
    return static_cast<std::uint8_t>((A&0x80u)|(result>>2u));
}

unsigned int groupBits(unsigned int radix, unsigned int count)
{
    static constexpr unsigned int trit[6] {0,2,4,5,7,8};
    static constexpr unsigned int quint[4] {0,3,5,7};
    return radix == 3u ? trit[count] : quint[count];
}

bool decodeEndpoints(const std::uint8_t *block, const Mode& mode, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!block || !out) return fail(error, "invalid UASTC endpoint output");
    const unsigned int count = static_cast<unsigned int>(mode.components) * 2u * mode.subsets;
    unsigned int value_bits = 0u, radix = 1u;
    if (!endpointInfo(mode.endpoint_range, &value_bits, &radix, error)) return false;
    std::vector<std::uint8_t> digits(count, 0u);
    unsigned int cursor = mode.endpoint_offset;
    if (radix != 1u) {
        const unsigned int group_size = radix == 3u ? 5u : 3u;
        for (unsigned int first=0u; first<count; first+=group_size) {
            const unsigned int n=std::min(group_size,count-first);
            const unsigned int gb=groupBits(radix,n);
            std::uint32_t packed=bits(block,cursor,gb); cursor+=gb;
            std::uint32_t limit=1u;
            for (unsigned int i=0u;i<n;++i) limit*=radix;
            if (packed>=limit) return fail(error,"invalid UASTC BISE trit/quint group");
            for (unsigned int i=0u;i<n;++i) { digits[first+i]=static_cast<std::uint8_t>(packed%radix); packed/=radix; }
        }
    }
    out->resize(count);
    for (unsigned int i=0u;i<count;++i) {
        const std::uint32_t low=bits(block,cursor,value_bits); cursor+=value_bits;
        const std::uint32_t encoded=low+(static_cast<std::uint32_t>(digits[i])<<value_bits);
        (*out)[i]=unquantize(mode.endpoint_range,encoded);
    }
    if (cursor>mode.weight_offset) return fail(error,"UASTC endpoint fields overlap weights");
    return true;
}

std::uint8_t dequantWeight(unsigned int n, std::uint32_t value)
{
    static constexpr std::array<std::uint8_t,2> w1 {{0,64}};
    static constexpr std::array<std::uint8_t,4> w2 {{0,21,43,64}};
    static constexpr std::array<std::uint8_t,8> w3 {{0,9,18,27,37,46,55,64}};
    static constexpr std::array<std::uint8_t,16> w4 {{0,4,8,12,17,21,25,29,35,39,43,47,52,56,60,64}};
    static constexpr std::array<std::uint8_t,32> w5 {{0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64}};
    if (n==1u) return w1[value&1u]; if (n==2u) return w2[value&3u]; if (n==3u) return w3[value&7u];
    if (n==4u) return w4[value&15u]; return w5[value&31u];
}

std::uint8_t interpolate(std::uint8_t low, std::uint8_t high, std::uint8_t weight)
{
    const std::uint32_t l=(static_cast<std::uint32_t>(low)<<8u)|low;
    const std::uint32_t h=(static_cast<std::uint32_t>(high)<<8u)|high;
    return static_cast<std::uint8_t>(((l*(64u-weight)+h*weight+32u)>>6u)>>8u);
}

bool decodeWeights(
    const std::uint8_t *block,
    const Mode& mode,
    const Pattern *pattern,
    const std::array<std::uint8_t,3>& anchors,
    std::array<std::array<std::uint8_t,2>,16> *weights,
    std::string *error)
{
    if (!block || !weights || mode.weight_bits==0u) return fail(error,"invalid UASTC weight decoder state");
    unsigned int cursor=mode.weight_offset;
    for (unsigned int texel=0u;texel<16u;++texel) {
        const unsigned int subset=pattern?(*pattern)[texel]:0u;
        if (subset>=mode.subsets) return fail(error,"UASTC partition references invalid subset");
        const bool anchor=texel==anchors[subset];
        for (unsigned int plane=0u;plane<mode.planes;++plane) {
            const unsigned int n=mode.weight_bits-(anchor?1u:0u);
            const std::uint32_t index=bits(block,cursor,n); cursor+=n;
            (*weights)[texel][plane]=dequantWeight(mode.weight_bits,index);
        }
    }
    if (cursor>128u) return fail(error,"UASTC weight data exceeds block");
    return true;
}

bool findMode(const std::uint8_t *block, std::size_t *index, std::string *error)
{
    if (!block || !index) return fail(error,"invalid UASTC block");
    for (std::size_t i=0u;i<modes.size();++i) {
        const Mode& mode=modes[i];
        if (bits(block,0u,mode.code_bits)==mode.code) { *index=i; return true; }
    }
    if (bits(block,0u,7u)==0x45u) return fail(error,"reserved UASTC mode 19");
    return fail(error,"invalid UASTC mode prefix");
}

} // namespace

bool decodeBlock(const std::uint8_t *block, std::uint8_t rgba[64], std::string *error)
{
    if (error) error->clear();
    if (!block || !rgba) return fail(error,"null UASTC block/output");
    std::size_t mode_index=0u;
    if (!findMode(block,&mode_index,error)) return false;
    const Mode& mode=modes[mode_index];
    if (mode_index==8u) {
        const std::uint8_t r=static_cast<std::uint8_t>(bits(block,5u,8u));
        const std::uint8_t g=static_cast<std::uint8_t>(bits(block,13u,8u));
        const std::uint8_t b=static_cast<std::uint8_t>(bits(block,21u,8u));
        const std::uint8_t a=static_cast<std::uint8_t>(bits(block,29u,8u));
        for (unsigned int i=0u;i<16u;++i) { rgba[i*4u]=r;rgba[i*4u+1u]=g;rgba[i*4u+2u]=b;rgba[i*4u+3u]=a; }
        return true;
    }

    const std::uint32_t pattern_index=mode.pattern_kind?bits(block,mode.pattern_offset,mode.pattern_bits):0u;
    std::array<std::uint8_t,3> anchors {{0,0,0}};
    const Pattern *pattern=patternFor(mode,pattern_index,&anchors,error);
    if (mode.pattern_kind!=0u && !pattern) return false;
    std::vector<std::uint8_t> endpoints;
    if (!decodeEndpoints(block,mode,&endpoints,error)) return false;
    std::array<std::array<std::uint8_t,2>,16> weights{};
    if (!decodeWeights(block,mode,pattern,anchors,&weights,error)) return false;

    int component_selector=-1;
    if (mode.planes==2u) {
        if (mode_index==17u) component_selector=1;
        else {
            if (mode.component_offset<0) return fail(error,"UASTC dual-plane mode lacks component selector");
            component_selector=static_cast<int>(bits(block,static_cast<unsigned int>(mode.component_offset),2u));
            if ((mode.components==3u && component_selector>2) || component_selector>=mode.components)
                return fail(error,"invalid UASTC dual-plane component selector");
        }
    }

    for (unsigned int texel=0u;texel<16u;++texel) {
        const unsigned int subset=pattern?(*pattern)[texel]:0u;
        const std::size_t base=static_cast<std::size_t>(subset)*mode.components*2u;
        std::array<std::uint8_t,4> color {{0,0,0,255}};
        if (mode.components==2u) {
            const std::uint8_t wl=weights[texel][0];
            const std::uint8_t wa=mode.planes==2u?weights[texel][1]:wl;
            const std::uint8_t l=interpolate(endpoints[base],endpoints[base+1u],wl);
            color[0]=color[1]=color[2]=l;
            color[3]=interpolate(endpoints[base+2u],endpoints[base+3u],wa);
        } else {
            for (unsigned int component=0u;component<mode.components;++component) {
                const unsigned int plane=mode.planes==2u && static_cast<int>(component)==component_selector?1u:0u;
                color[component]=interpolate(
                    endpoints[base+component*2u],
                    endpoints[base+component*2u+1u],
                    weights[texel][plane]
                );
            }
        }
        for (unsigned int component=0u;component<4u;++component) rgba[texel*4u+component]=color[component];
    }
    return true;
}

bool decodeImage(
    const std::uint8_t *blocks,
    std::size_t size,
    int width,
    int height,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!blocks || !image || width<=0 || height<=0) return fail(error,"invalid UASTC image arguments");
    const std::size_t blocks_x=(static_cast<std::size_t>(width)+3u)/4u;
    const std::size_t blocks_y=(static_cast<std::size_t>(height)+3u)/4u;
    if (blocks_x>std::numeric_limits<std::size_t>::max()/blocks_y || blocks_x*blocks_y>size/16u)
        return fail(error,"UASTC image payload is truncated");
    const std::size_t pixels=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
    if (static_cast<std::size_t>(width)>std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(height) ||
        pixels>std::numeric_limits<std::size_t>::max()/4u)
        return fail(error,"UASTC image dimensions overflow");
    image->width=width; image->height=height; image->rgba.assign(pixels*4u,0u); image->meaningful_alpha=false;
    std::uint8_t decoded[64]{};
    for (std::size_t by=0u;by<blocks_y;++by) {
        for (std::size_t bx=0u;bx<blocks_x;++bx) {
            const std::size_t block_index=by*blocks_x+bx;
            if (!decodeBlock(blocks+block_index*16u,decoded,error)) return false;
            for (unsigned int y=0u;y<4u;++y) for (unsigned int x=0u;x<4u;++x) {
                const std::size_t px=bx*4u+x, py=by*4u+y;
                if (px>=static_cast<std::size_t>(width)||py>=static_cast<std::size_t>(height)) continue;
                const std::size_t source=(y*4u+x)*4u, destination=(py*static_cast<std::size_t>(width)+px)*4u;
                for (unsigned int c=0u;c<4u;++c) image->rgba[destination+c]=decoded[source+c];
                if (decoded[source+3u]!=255u) image->meaningful_alpha=true;
            }
        }
    }
    return true;
}

} // namespace Models::Images::Uastc
