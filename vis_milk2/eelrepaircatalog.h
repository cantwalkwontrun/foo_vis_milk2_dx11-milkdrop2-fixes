// Verified legacy expression completions. Language: en-GB.
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string_view>
namespace PresetEelCompat::Catalogue
{
// Exact cut-off lines corroborated by intact variants. Used after compilation
// fails and at source-line boundaries; no preset filename or content-ID checks.
struct Entry { std::string_view broken; std::string_view complete; };
inline constexpr Entry Truncated[] = {
    {"state=if(above(bass_att,mid),if(above(bass_att,treb_att),3,2),if(abo",
     "state=if(above(bass_att,mid),if(above(bass_att,treb_att),3,2),if(above(treb_att,mid),2,1));"},
    {"beat=if(above(volume,0.8),if(below(peakbass_att-bass_att,0.05*",
     "beat=if(above(volume,0.8),if(below(peakbass_att-bass_att,0.05*peakbass_att),if(above(time-lastbeat,0.1+0.5*(beatrate-0.1)),1,0),0),0);"},
    {"beatrate=max(if(beat,if(below(time-lastbeat,2*beatrate),0.1*(beatrate*9+",
     "beatrate=max(if(beat,if(below(time-lastbeat,2*beatrate),0.1*(beatrate*9+time-lastbeat),beatrate),beatrate),0.1);"},
    {"peakbass_att=if(equal(beat,0),if(above(time-lastbeat,2*beatrate)",
     "peakbass_att=if(equal(beat,0),if(above(time-lastbeat,2*beatrate),peakbass_att*0.95,peakbass_att*0.995),bass_att);"},
    {"mycounter=if(beateven,if(beat,if(above(mycounter,4),0,mycounter+1),mycounter",
     "mycounter=if(beateven,if(beat,if(above(mycounter,4),0,mycounter+1),mycounter),mycounter);"},
    {"q6=oldq6+0.005*(pow(1+1.2*bass+0.4*bass_att+0.1*treb+0.1*treb_att+0.1*mid+0.",
     "q6=oldq6+0.005*(pow(1+1.2*bass+0.4*bass_att+0.1*treb+0.1*treb_att+0.1*mid+0.1*mid_att,6)/fps);"},
    {"newzoom=pow(0.999+0.05*sin((q4)*(newang+q3)),pow(sin(newrad*newrad),",
     "newzoom=pow(0.999+0.05*sin((q4)*(newang+q3)),pow(sin(newrad*newrad),newrad));"},
};
inline constexpr std::string_view SplitDecimalZoom[] = {
    "zoom=zoom+(y*2-1)*(0.08+0.52*sin(time*0.618))-(x*2-1)*(0.06+0.22*sin(time*0.851));",
    "zoom=zoom+(y*2-1)*(0.06+0.52*sin(time*1.618))-(x*2-1)*(0.06+0.12*sin(time*2.851));",
};
// These four complete statements differ from intact neighbours only by a
// duplicated random-function wrapper. Unknown result-as-function calls do not match.
inline constexpr Entry RandomWrapper[] = {
    {"md=(md+int(rand(4))(4))%4;", "md=(md+int(rand(4)))%4;"},
    {"x=.3+(int(rand(4))(10)*.3);", "x=.3+(int(rand(10))*.3);"},
    {"cx=if(above(bass,1.4),int(rand(4))(10)*.1,cx);", "cx=if(above(bass,1.4),int(rand(10))*.1,cx);"},
    {"cy=if(above(treb,1.4),int(rand(4))(10)*.1,cy);", "cy=if(above(treb,1.4),int(rand(10))*.1,cy);"},
};
}
