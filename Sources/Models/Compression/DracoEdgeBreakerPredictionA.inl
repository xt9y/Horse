{
    if(!out||!state.position_record||!state.position_group||state.position_record->portable.empty()||corner<0)return false;
    const DecoderGroup& group=*state.position_group;
    if(static_cast<std::size_t>(corner)>=group.corner_vertex.size())return false;
    const std::uint32_t domain=group.corner_vertex[static_cast<std::size_t>(corner)];
    if(domain>=group.domain_to_entry.size())return false;
    const std::uint32_t entry=group.domain_to_entry[domain];
    if(entry==UINT32_MAX || (static_cast<std::size_t>(entry)+1u)*3u>state.position_record->portable.size())return false;
    (*out)[0]=state.position_record->portable[static_cast<std::size_t>(entry)*3u+0u];
    (*out)[1]=state.position_record->portable[static_cast<std::size_t>(entry)*3u+1u];
    (*out)[2]=state.position_record->portable[static_cast<std::size_t>(entry)*3u+2u];
    return true;
}

void canonicalizeIntegerVector(std::array<std::int64_t,3> *value,std::int64_t center)
{
    const std::int64_t sum=std::llabs((*value)[0])+std::llabs((*value)[1])+std::llabs((*value)[2]);
    if(sum==0){(*value)[0]=center;return;}
    (*value)[0]=((*value)[0]*center)/sum;
    (*value)[1]=((*value)[1]*center)/sum;
    (*value)[2]=(*value)[2]>=0 ? center-std::llabs((*value)[0])-std::llabs((*value)[1]) : -(center-std::llabs((*value)[0])-std::llabs((*value)[1]));
}

void canonicalizeOctahedral(std::int64_t *s,std::int64_t *t,std::int64_t center,std::int64_t max_value)
{
    if((*s==0&&*t==0)||(*s==0&&*t==max_value)||(*s==max_value&&*t==0)){*s=max_value;*t=max_value;}
    else if(*s==0&&*t>center)*t=center-(*t-center);
    else if(*s==max_value&&*t<center)*t=center+(center-*t);
    else if(*t==max_value&&*s<center)*s=center+(center-*s);
    else if(*t==0&&*s>center)*s=center-(*s-center);
}

std::array<std::int64_t,2> integerVectorToOct(std::array<std::int64_t,3> value,std::int64_t center,std::int64_t max_value)
{
    std::int64_t s=0,t=0;
    if(value[0]>=0){s=value[1]+center;t=value[2]+center;}
    else{
        s=value[1]<0?std::llabs(value[2]):max_value-std::llabs(value[2]);
        t=value[2]<0?std::llabs(value[1]):max_value-std::llabs(value[1]);
    }
    canonicalizeOctahedral(&s,&t,center,max_value);return{s,t};
}

void invertDiamond(std::int64_t *s,std::int64_t *t,std::int64_t center)
{
    const std::int64_t sign_s=*s>=0?1:-1,sign_t=*t>=0?1:-1;
    const std::int64_t cs=sign_s*center,ct=sign_t*center;
    *s=2*(*s)-cs;*t=2*(*t)-ct;
    if(sign_s*sign_t>=0){const std::int64_t tmp=*s;*s=-*t;*t=-tmp;}
    else{const std::int64_t tmp=*s;*s=*t;*t=tmp;}
    *s=(*s+cs)/2;*t=(*t+ct)/2;
}

int rotationCount(std::int64_t x,std::int64_t y)
{
    if(x==0)return y==0?0:(y>0?3:1);
    if(x>0)return y>=0?2:1;
    return y<=0?0:3;
}

std::array<std::int64_t,2> rotatePoint(std::array<std::int64_t,2> p,int count)
{
    switch(count&3){case 1:return{p[1],-p[0]};case 2:return{-p[0],-p[1]};case 3:return{-p[1],p[0]};default:return p;}
}

std::int64_t modMax(std::int64_t x,std::int64_t center,std::int64_t max_quantized)
{
    if (x > center) return x - max_quantized;
    if (x < -center) return x + max_quantized;
    return x;
}

std::array<std::int32_t,2> normalTransform(std::array<std::int32_t,2> pred,std::array<std::int32_t,2> corr,std::int32_t encoded_max)
{
    unsigned int bits=0u;std::uint32_t value=encoded_max>0?static_cast<std::uint32_t>(encoded_max):0u;while(value){++bits;value>>=1u;}if(bits==0u)bits=1u;
    const std::int64_t max_quantized=(std::int64_t{1}<<bits)-1, max_value=max_quantized-1, center=max_value/2;
    std::array<std::int64_t,2> p={static_cast<std::int64_t>(pred[0])-center,static_cast<std::int64_t>(pred[1])-center};
    const bool in_diamond=std::llabs(p[0])+std::llabs(p[1])<=center;
    if(!in_diamond)invertDiamond(&p[0],&p[1],center);
    const bool bottom_left=(p[0]==0&&p[1]==0)||(p[0]<0&&p[1]<=0);
    const int rotation=rotationCount(p[0],p[1]);if(!bottom_left)p=rotatePoint(p,rotation);
    p[0]=modMax(p[0]+corr[0],center,max_quantized);p[1]=modMax(p[1]+corr[1],center,max_quantized);
    if (!bottom_left) p = rotatePoint(p, (4 - rotation) % 4);
    if (!in_diamond) invertDiamond(&p[0], &p[1], center);
    return{static_cast<std::int32_t>(p[0]+center),static_cast<std::int32_t>(p[1]+center)};
}

bool decodeIntegerAttribute(Reader& reader,DecodeState *state,DecoderGroup& group,AttributeRecord *record,std::string *error)
{
    if(!state||!record)return false;
    const unsigned int portable_components=record->decoder_type==3u?2u:record->output.components;
    const std::size_t entries=group.sequence_corners.size();
    if(portable_components==0u || entries>std::numeric_limits<std::size_t>::max()/portable_components)
        return fail(error,"Draco attribute value count overflows");
    const std::size_t value_count=entries*portable_components;
    std::int8_t prediction=-2,transform=0;
    if(!reader.i8(&prediction))return fail(error,"truncated Draco prediction method");
    if(prediction!=-2 && !reader.i8(&transform))return fail(error,"truncated Draco prediction transform");
    if(prediction!=-2 && transform!=1 && transform!=3)return fail(error,"unsupported Draco prediction transform");
    std::uint8_t compressed=0u;if(!reader.u8(&compressed))return fail(error,"truncated Draco integer compression flag");
    std::vector<std::uint32_t> symbols(value_count,0u);
    if(compressed!=0u){if(!decodeSymbols(reader,value_count,portable_components,&symbols))return fail(error,"invalid Draco integer symbol stream");}
    else{
        std::uint8_t bytes=0u;if(!reader.u8(&bytes)||bytes==0u||bytes>4u)return fail(error,"invalid Draco integer byte width");
        for(std::uint32_t& value:symbols)if(!littleUnsigned(reader,bytes,&value))return fail(error,"truncated Draco integer data");
    }
    std::vector<std::int32_t> corrections(value_count,0);
    const bool positive_corrections=prediction!=-2&&transform==3;
    for(std::size_t i=0u;i<value_count;++i) corrections[i]=positive_corrections?static_cast<std::int32_t>(symbols[i]):zigzag(symbols[i]);

    std::int32_t wrap_min=0,wrap_max=0,normal_max_q=0;
    std::vector<std::array<std::vector<std::uint8_t>,4>> unused;
    std::array<std::vector<std::uint8_t>,4> crease_bits;
    std::vector<std::uint8_t> orientations,flip_bits;
    if(prediction==4){
        for(std::size_t context=0u;context<4u;++context){std::uint32_t count=0u;if(!reader.var32(&count))return fail(error,"truncated Draco constrained prediction flags");if(count>0u&&!readPredictionBits(reader,count,&crease_bits[context]))return fail(error,"invalid Draco constrained prediction flags");}
    } else if(prediction==5){
        std::uint32_t count=0u;if(!reader.u32(&count))return fail(error,"truncated Draco texcoord orientation count");
        std::vector<std::uint8_t> bits;if(count>0u&&!readPredictionBits(reader,count,&bits))return fail(error,"invalid Draco texcoord orientation stream");
        orientations.reserve(bits.size());bool last=true;for(const std::uint8_t bit:bits){if(bit==0u)last=!last;orientations.push_back(last?1u:0u);}
    } else if(prediction==6){
        if(transform!=3)return fail(error,"Draco geometric normal prediction requires canonicalized octahedral transform");
        std::int32_t unused_center=0;if(!reader.i32(&normal_max_q)||!reader.i32(&unused_center))return fail(error,"truncated Draco normal prediction transform");(void)unused_center;
        if(!readPredictionBits(reader,entries,&flip_bits))return fail(error,"invalid Draco normal flip stream");
    }
    if(prediction!=-2 && prediction!=6){
        if(transform==1){if(!reader.i32(&wrap_min)||!reader.i32(&wrap_max)||wrap_min>wrap_max)return fail(error,"invalid Draco wrap transform");}
        else if(transform==3){std::int32_t unused_center=0;if(!reader.i32(&normal_max_q)||!reader.i32(&unused_center))return fail(error,"truncated Draco normal transform");}
    }

    std::vector<std::int32_t> original(value_count,0);
    auto correctionAt=[&](std::size_t entry){std::vector<std::int32_t> v(portable_components,0);for(unsigned int c=0u;c<portable_components;++c)v[c]=corrections[entry*portable_components+c];return v;};
    auto writeEntry=[&](std::size_t entry,const std::vector<std::int32_t>& value){for(unsigned int c=0u;c<portable_components;++c)original[entry*portable_components+c]=value[c];};
    auto previousValue=[&](std::size_t entry){std::vector<std::int32_t> v(portable_components,0);if(entry>0u)for(unsigned int c=0u;c<portable_components;++c)v[c]=original[(entry-1u)*portable_components+c];return v;};
    auto applyWrap=[&](const std::vector<std::int32_t>& pred,const std::vector<std::int32_t>& corr){std::vector<std::int32_t> out;wrapValue(pred,corr,wrap_min,wrap_max,&out);return out;};

    if(prediction==-2){original=corrections;}
    else if(prediction==0){
        for(std::size_t entry=0u;entry<entries;++entry)writeEntry(entry,applyWrap(previousValue(entry),correctionAt(entry)));
    } else if(prediction==1){
        for(std::size_t entry=0u;entry<entries;++entry){std::vector<std::int32_t> pred;if(entry==0u||!parallelogramPrediction(*state->topology,group,entry,original,portable_components,&pred))pred=previousValue(entry);writeEntry(entry,applyWrap(pred,correctionAt(entry)));}
    } else if(prediction==4){
        std::array<std::size_t,4> crease_pos{};
        for(std::size_t entry=0u;entry<entries;++entry){
            std::vector<std::vector<std::int32_t>> predictions;
            if(entry>0u){
                const int start=group.sequence_corners[entry];int corner=start;bool first_pass=true;
                while(corner>=0){std::vector<std::int32_t> pred;if(parallelogramPrediction(*state->topology,group,entry,original,portable_components,&pred))predictions.push_back(std::move(pred));if(predictions.size()==4u)break;
                    int next=first_pass?([&]{const int e=nextCorner(corner);const int o=groupOpposite(*state->topology,group,e);return o<0?-1:nextCorner(o);}()):([&]{const int e=previousCorner(corner);const int o=groupOpposite(*state->topology,group,e);return o<0?-1:previousCorner(o);}());
                    if (next == start) break;
                    if (next < 0 && first_pass) {
                        first_pass = false;
                        const int edge = previousCorner(start);
                        const int opposite = groupOpposite(*state->topology, group, edge);
                        next = opposite < 0 ? -1 : previousCorner(opposite);
                    }
                    corner = next;
                }
            }
            std::vector<std::int32_t> pred(portable_components,0);std::size_t used=0u;
