/*
 * rules.cpp
 *
 */
#include "ntfw.h"
#include "resource.h"
#include "helper.h"
#include <windowsx.h>
#include <unordered_map>
#include <map>
#include <string>
#include <stdlib.h>




// Constants
#define RULE_PAGE_SZ  8
#define MAX_RULES  32
#define MAX_PROTO_NAME  16

// Helpers for control referencing.
#define GetCtl(id) GetDlgItem(hwndDialog, id)
#define CtlProto(idx) GetCtl(IDC_COMBO1 + idx)
#define CtlPort(idx) GetCtl(IDC_EDIT1 + idx)
#define CtlByte(idx) GetCtl(IDC_EDIT9 + idx)
#define CtlPacket(idx) GetCtl(IDC_EDIT17 + idx)
#define CtlSyn(idx) GetCtl(IDC_EDIT25 + idx)
#define CtlByteMeter(idx) GetCtl(IDC_COMBO9 + idx)
#define CtlPacketMeter(idx) GetCtl(IDC_COMBO17 + idx)
#define CtlSynMeter(idx) GetCtl(IDC_COMBO25 + idx)

// Common protocols enumerations.
std::unordered_map<std::string, u8> str_to_l4 =
{
    { "icmp", IP_P_ICMP },
    { "igmp", IP_P_IGMP },
    { "ipip", IP_P_IPIP },
    { "tcp",  IP_P_TCP },
    { "egp",  IP_P_EGP },
    { "pup",  IP_P_PUP },
    { "udp",  IP_P_UDP },
    { "dccp", IP_P_DCCP },
    { "rsvp", IP_P_RSVP },
    { "sctp", IP_P_SCTP },
    { "ldp",  IP_P_LDP },
};


std::unordered_map<u8, std::string> l4_to_str =
{
    { IP_P_ICMP, "icmp" },
    { IP_P_IGMP, "igmp" },
    { IP_P_IPIP, "ipip" },
    { IP_P_TCP,  "tcp" },
    { IP_P_EGP,  "egp" },
    { IP_P_PUP,  "pup" },
    { IP_P_UDP,  "udp" },
    { IP_P_DCCP, "dccp" },
    { IP_P_RSVP, "rsvp" },
    { IP_P_SCTP, "sctp" },
    { IP_P_LDP,  "ldp" },
};

std::unordered_map<std::string, u16> str_to_l3 =
{
    { "ip",     ETH_P_IP },
    { "arp",    ETH_P_ARP },
    { "ip6",    ETH_P_IPV6 },
    { "ipx",    ETH_P_IPX },
    { "802.1q", ETH_P_8021Q },
    { "ptp",    ETH_P_PTP },
};

std::unordered_map<u16, std::string> l3_to_str =
{
    { ETH_P_IP,    "ip" },
    { ETH_P_ARP,   "arp" },
    { ETH_P_IPV6,  "ip6" },
    { ETH_P_IPX,   "ipx" },
    { ETH_P_8021Q, "802.1q" },
    { ETH_P_PTP,   "ptp" },
};

// Storing control retained state internally.
struct Rule
{
    char Proto[MAX_PROTO_NAME];
    u16 Port;
    u16 ByteCost;
    u16 PacketCost;
    u16 SynCost;
    u16 ByteMeter : 2,
        PacketMeter : 2,
        SynMeter : 2;
} Rules[MAX_RULES];

static HWND hwndDialog;



static void Synchronize(int Idx, Rule& rl, bool bDown)
{
    if(bDown)
    {
        ComboBox_GetText(CtlProto(Idx), rl.Proto, sizeof(rl.Proto));
        rl.Port = (u16) Edit_GetTextInt(CtlPort(Idx));
        rl.ByteCost = (u16) Edit_GetTextInt(CtlByte(Idx));
        rl.PacketCost = (u16) Edit_GetTextInt(CtlPacket(Idx));
        rl.SynCost = (u16) Edit_GetTextInt(CtlSyn(Idx));
        rl.ByteMeter = (u16) Edit_GetTextInt(CtlByteMeter(Idx));
        rl.PacketMeter = (u16) Edit_GetTextInt(CtlPacketMeter(Idx));
        rl.SynMeter = (u16) Edit_GetTextInt(CtlSynMeter(Idx));
    }

    else
    {
        ComboBox_SetText(CtlProto(Idx), rl.Proto);
        Edit_SetTextInt(CtlPort(Idx), rl.Port);
        Edit_SetTextInt(CtlByte(Idx), rl.ByteCost);
        Edit_SetTextInt(CtlPacket(Idx), rl.PacketCost);
        Edit_SetTextInt(CtlSyn(Idx), rl.SynCost);
        Edit_SetTextInt(CtlByteMeter(Idx), rl.ByteMeter);
        Edit_SetTextInt(CtlPacketMeter(Idx), rl.PacketMeter);
        Edit_SetTextInt(CtlSynMeter(Idx), rl.SynMeter);
    }
}


static void Scroll(int CurPos, int NewPos, BOOL bRedraw)
{
    if(NewPos < 0)
        NewPos = 0;

    NewPos = min(NewPos, MAX_RULES - RULE_PAGE_SZ);

    SetScrollPos(GetDlgItem(hwndDialog, IDC_SB_RULE), SB_CTL, NewPos, bRedraw);

    for(int i = 0; i < RULE_PAGE_SZ; i++)
        Synchronize(i, Rules[CurPos+i], true);
        
    for(int i = 0; i < RULE_PAGE_SZ; i++)
        Synchronize(i, Rules[NewPos+i], false);
}


void RulesUpload(ntfe_config* pCfg)
{
    union
    {
        ntfe_rule* src;
        ntfe_meter* meter;
    }; src = (ntfe_rule*) pCfg->ect;

    memzero(Rules, sizeof(Rules));

    for(uint i = 0; i < pCfg->rule_count; i++, src++)
    {
        Rule& dst = Rules[i];

        if(src->ether != ETH_P_IP)
            l3_to_str.count(src->ether) ? strcpy(dst.Proto, l3_to_str[src->ether].c_str()) : (char*) sprintf(dst.Proto, "%X", src->ether);

        else
            l4_to_str.count(src->proto) ? strcpy(dst.Proto, l4_to_str[src->proto].c_str()) : (char*) sprintf(dst.Proto, "%X", src->proto);

        dst.Port = src->dport;
        dst.ByteCost = src->byte_scale;
        dst.PacketCost = src->packet_scale;
        dst.SynCost = src->syn_scale;
        dst.ByteMeter = src->byte_tb;
        dst.PacketMeter = src->packet_tb;
        dst.SynMeter = src->syn_tb;
    }

    for(int i = 0; i < RULE_PAGE_SZ; i++)
        Synchronize(i, Rules[i], false);

    ScrollBar_SetPos(GetDlgItem(hwndDialog, IDC_SB_RULE), 0, TRUE);
}



bool RulesDownload(ntfe_config* pCfg)
{
    union
    {
        ntfe_rule* dst;
    }; dst = (ntfe_rule*) pCfg->ect;


    for(int i = 0, c = ScrollBar_GetPos(GetDlgItem(hwndDialog, IDC_SB_RULE)); i < RULE_PAGE_SZ; i++)
        Synchronize(i, Rules[c+i], true);

    pCfg->rule_count = 0;

    for(uint i = 0; i < MAX_RULES; i++)
    {
        Rule& src = Rules[i];

        // Normalize protocol to uppercase and check it's not empty, which
        // we interpret as an undefined entry.
        {
            u8 v = 0;

            for(u8* p = (u8*) src.Proto; *p; p++)
            {
                *p |= 0x20;
                v |= *p;
            }

            if(!(v & 0xE0))
                continue;
        }

        if(str_to_l4.count(src.Proto))
        {
            dst->ether = ETH_P_IP;
            dst->proto = str_to_l4[src.Proto];
            dst->dport = src.Port;
        }

        else if(str_to_l3.count(src.Proto))
        {
            dst->ether = str_to_l3[src.Proto];
            dst->proto = 0;
            dst->dport = 0;
        }

        else
        {
            // Must be the hexadecimal representation of either an L3 or L4
            // protocol. The two can be differentiated by their minimum widths:
            // an L3 protocol must be atleast 3 digits, anything smaller and it
            // must be L4.
            u8 digits = 0;

            dst->ether = 0;
            dst->proto = 0;
            dst->dport = 0;

            for(char* p = src.Proto; *p; p++, digits++)
            {
                if(!((*p >= '0') && (*p <= '9') || ((*p >= 'a') && (*p <= 'f'))) || digits > 4)
                {
                    NotifyUser(MB_ICONERROR | MB_OK, "Error", "Unrecognized custom protocol syntax in rule #%u\n\nValue was '%s'", 1+i, src.Proto);
                    return false;
                }

                dst->ether <<= 4;
                dst->ether += (*p & 0x40 ? (*p & 0x0F) + 9 : (*p & 0x0F));
            }

            if(!(dst->ether & 0xFF00))
            {
                dst->proto = (u8) dst->ether;
                dst->ether = ETH_P_IP;
            }
        }

        // The rest is straightforward.
        dst->byte_scale = src.ByteCost;
        dst->packet_scale = src.PacketCost;
        dst->syn_scale = src.SynCost;
        dst->byte_tb = src.ByteMeter;
        dst->packet_tb = src.PacketMeter;
        dst->syn_tb = src.SynMeter;

        pCfg->rule_count++;
        dst++;
    }
}




INT_PTR CALLBACK OnRulesDlgMsg(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    switch(message)
    {
        case WM_INITDIALOG:
        {
            ::hwndDialog = hwnd;

            // Populate control static data.
            for(int i = 0; i < RULE_PAGE_SZ; i++)
            {
                // Protocols
                ComboBox_AddString(CtlProto(i), ">> L4 Protocols");

                for(const auto& p : str_to_l4)
                    ComboBox_AddString(CtlProto(i), p.first.c_str());

                ComboBox_AddString(CtlProto(i), ">> L3 Protocols");

                for(const auto& p : str_to_l3)
                    ComboBox_AddString(CtlProto(i), p.first.c_str());

                // Meter identifiers.
                for(u16 k = '1'; k < '5'; k++)
                {
                    ComboBox_AddString(CtlByteMeter(i), &k);
                    ComboBox_AddString(CtlPacketMeter(i), &k);
                    ComboBox_AddString(CtlSynMeter(i), &k);
                }
            }

            SCROLLINFO si;
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
            si.nPage = RULE_PAGE_SZ;
            si.nMin = 0;
            si.nMax = MAX_RULES - 1;
            si.nPos = 0;

            SetScrollInfo(GetDlgItem(hwnd, IDC_SB_RULE), SB_CTL, &si, FALSE);

            return true;
        }

        case WM_MOUSEWHEEL:
        {
            s16 dt = HIWORD(wParam);
            int CurPos = ScrollBar_GetPos(GetDlgItem(hwnd, IDC_SB_RULE));

            Scroll(CurPos, CurPos - (dt / WHEEL_DELTA), TRUE);

        } break;

        case WM_VSCROLL:
        {
            auto CurPos = ScrollBar_GetPos(GetDlgItem(hwnd, IDC_SB_RULE));
            auto NewPos = CurPos;

            switch(LOWORD(wParam))
            {
                case SB_ENDSCROLL: return true;
                case SB_LINEDOWN: NewPos++; break;
                case SB_LINEUP: NewPos--; break;
                case SB_PAGEDOWN: NewPos += RULE_PAGE_SZ; break;
                case SB_PAGEUP: NewPos -= RULE_PAGE_SZ; break;

                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:
                    NewPos = HIWORD(wParam);

            }

            Scroll(CurPos, NewPos, FALSE);

            return true;
        } break;


        case WM_COMMAND:
        {
            

            if(LOWORD(wParam) == IDC_TEST)
            {
                static union
                {
                    byte tmp[0x1000];
                    ntfe_config cfg;
                };

                RulesDownload(&cfg);

                RulesUpload(&cfg);
            }
        }


    }



    return false;
}
