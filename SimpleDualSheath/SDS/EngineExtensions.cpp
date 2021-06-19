#include "pch.h"

#include "EngineExtensions.h"
#include "SDS/Data.h"
#include "SDS/Util/Common.h"

#include <ext/IHook.h>
#include <ext/JITASM.h>
#include <ext/Patching.h>
#include <ext/VFT.h>

namespace SDS
{
    using namespace JITASM;
    using namespace Events;
    using namespace Util;

    std::unique_ptr<EngineExtensions> EngineExtensions::m_Instance;

    EngineExtensions::EngineExtensions(
        const std::shared_ptr<Controller>& a_controller)
    {
        Patch_CreateWeaponNodes();

        auto& config = a_controller->GetConfig();

        if (config.m_scb)
        {
            Patch_SCB_Attach();
            Patch_SCB_Detach();
        }

        if (config.m_shield.IsEnabled())
        {
            Patch_Object_Attach();
            if (config.m_shieldHandWorkaround) {
                if (!Patch_ShieldHandWorkaround()) {
                    Error("Shield hand patches failed");
                }
            }
        }

        if ((config.m_shieldHideFlags & Data::Flags::kEnabled) != Data::Flags::kNone)
        {
            Patch_DisableShieldHideOnSit(config);
        }

        m_dispatchers.m_createWeaponNodes.AddSink(a_controller.get());

        if (config.HasEnabled2HEntries())
        {
            if (Hook_TESObjectWEAP_SetEquipSlot()) {
                m_dispatchers.m_setEquipSlot.AddSink(a_controller.get());
            }
        }

        m_controller = a_controller;
    }

    void EngineExtensions::Initialize(
        const std::shared_ptr<Controller>& a_controller)
    {
        if (m_Instance.get() == nullptr) {
            m_Instance = std::unique_ptr<EngineExtensions>{ new EngineExtensions(a_controller) };
        }
    }

    auto EngineExtensions::ValidateMemory(
        const Config& a_config) ->
        MemoryValidationFlags
    {
        using namespace Patching;

        MemoryValidationFlags result(MemoryValidationFlags::kNone);

        constexpr std::uint8_t d_createWeaponNodes[]{ 0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56 };
        if (!validate_mem(m_createWeaponNodes_a, d_createWeaponNodes)) {
            result |= MemoryValidationFlags::kCreateWeaponNodes;
        }

        if (a_config.m_shield.IsEnabled())
        {
            constexpr std::uint8_t
                d_createArmorNodev1[]{ 0x48, 0x85, 0xC0, 0x74, 0x0D },
                d_createArmorNodev2[]{ 0xE9 };

            if (!validate_mem(m_createArmorNode_a, d_createArmorNodev1) &&
                !validate_mem(m_createArmorNode_a, d_createArmorNodev2))
            {
                result |= MemoryValidationFlags::kCreateArmorNode;
            }
        }

        if ((a_config.m_shieldHideFlags & Data::Flags::kEnabled) != Data::Flags::kNone)
        {
            constexpr std::uint8_t d_hideShield[]{ 0x4C, 0x8B, 0x0A, 0x48, 0x8B, 0x81, 0xF0, 0x01, 0x00, 0x00 };
            if (!validate_mem(m_hideShield_a, d_hideShield)) {
                result |= MemoryValidationFlags::kDisableShieldHideOnSit;
            }
        }

        if (a_config.m_scb)
        {
            constexpr std::uint8_t d_scbAttach[]{ 0x80, 0x7D, 0x6F, 0x00, 0x75, 0x1E };
            if (!validate_mem(m_scbAttach_a, d_scbAttach)) {
                result |= MemoryValidationFlags::kScabbardAttach;
            }

            constexpr std::uint8_t d_scbDetach[]{ 0x0F, 0x84, 0xB3, 0x00, 0x00, 0x00 };
            if (!validate_mem(m_scbDetach_a, d_scbDetach)) {
                result |= MemoryValidationFlags::kScabbardDetach;
            }
        }

        return result;
    }

    void EngineExtensions::Patch_CreateWeaponNodes()
    {
        struct Assembly : JITASM
        {
            Assembly(std::uintptr_t a_targetAddr) :
                JITASM(ISKSE::GetLocalTrampoline())
            {
                Xbyak::Label retnLabel;

                db(reinterpret_cast<Xbyak::uint8*>(a_targetAddr), 0x7);
                jmp(ptr[rip + retnLabel]);

                L(retnLabel);
                dq(a_targetAddr + 0x7);
            }
        };

        LogPatchBegin(__FUNCTION__);
        {
            Assembly code(m_createWeaponNodes_a);
            m_createWeaponNodes_o = code.get<decltype(m_createWeaponNodes_o)>();
            ISKSE::GetBranchTrampoline().Write6Branch(m_createWeaponNodes_a, std::uintptr_t(CreateWeaponNodes_Hook));
        }
        LogPatchEnd(__FUNCTION__);
    }

    void EngineExtensions::Patch_SCB_Attach()
    {
        struct Assembly : JITASM
        {
            Assembly(std::uintptr_t targetAddr) :
                JITASM(ISKSE::GetLocalTrampoline())
            {
                Xbyak::Label callLabel;
                Xbyak::Label exitContinue;
                Xbyak::Label exitSkip;

                Xbyak::Label cont;
                Xbyak::Label skip;

                cmp(byte[rbp + 0x6F], 0);
                je(cont);

                mov(rcx, ptr[rbp + 0x77]);
                mov(rcx, ptr[rcx]);
                imul(rdx, r14, 0x78);
                mov(rdx, ptr[rcx + rdx + 0x10]); // form
                mov(rcx, ptr[rbp - 0x31]); // reference
                mov(r8, rsi); // attachment node

                push(rax);
                sub(rsp, 0x20);

                //mov(r9, rbx); // scb node

                call(ptr[rip + callLabel]);
                mov(rdx, rax);

                add(rsp, 0x20);
                pop(rax);

                test(rdx, rdx);
                je(skip);
                mov(rsi, rdx); // rsi: not used after scb is attached

                L(cont);
                jmp(ptr[rip + exitContinue]);

                L(skip);
                jmp(ptr[rip + exitSkip]);

                L(exitContinue);
                dq(targetAddr + 0x6);

                L(exitSkip);
                dq(targetAddr + 0x24);

                L(callLabel);
                dq(std::uintptr_t(GetScbAttachmentNode_Hook));
            }
        };

        LogPatchBegin(__FUNCTION__);
        {
            Assembly code(m_scbAttach_a);
            ISKSE::GetBranchTrampoline().Write6Branch(m_scbAttach_a, code.get());
        }
        LogPatchEnd(__FUNCTION__);
    }
    
    void EngineExtensions::Patch_SCB_Detach()
    {
        struct Assembly : JITASM
        {
            Assembly(std::uintptr_t targetAddr) :
                JITASM(ISKSE::GetLocalTrampoline())
            {
                Xbyak::Label callLabel;
                Xbyak::Label retnLabel;
                Xbyak::Label exitIsWeapon;
                Xbyak::Label exitIsShield;

                Xbyak::Label isWeaponSlot;
                Xbyak::Label ok;

                jne(isWeaponSlot);

                mov(rcx, ptr[rbp - 0x20]); // reference
                mov(rdx, ptr[rdi]); // form
                mov(r8, r12); // attachment node
                call(ptr[rip + callLabel]);
                test(rax, rax);
                jne(ok);
                jmp(ptr[rip + exitIsShield]);

                L(ok);
                mov(r12, rax);

                L(isWeaponSlot);
                jmp(ptr[rip + exitIsWeapon]);

                L(exitIsShield);
                dq(targetAddr + 0xB9);

                L(exitIsWeapon);
                dq(targetAddr + 0x6);

                L(callLabel);
                dq(std::uintptr_t(GetScbAttachmentNode_Hook));
            }
        };

        LogPatchBegin(__FUNCTION__);
        {
            Assembly code(m_scbDetach_a);
            ISKSE::GetBranchTrampoline().Write6Branch(m_scbDetach_a, code.get());
        }
        LogPatchEnd(__FUNCTION__);
    }

    void EngineExtensions::Patch_Object_Attach()
    {
        struct Assembly : JITASM
        {
            Assembly(std::uintptr_t targetAddr) :
                JITASM(ISKSE::GetLocalTrampoline())
            {
                Xbyak::Label callLabel;
                Xbyak::Label retnLabel;
                Xbyak::Label retnSkipLabel;

                Xbyak::Label exitSkip;

                cmp(r14d, 0xFFFFFFFF); // biped object == none
                je(exitSkip);

                mov(rcx, ptr[rbp - 0x31]); // reference
                test(rcx, rcx);
                je(exitSkip);

                cmp(byte[rcx + 0x1A], 0x3E); // is Character ?
                jne(exitSkip);
                mov(rax, ptr[rcx + 0x40]); // load baseform
                test(rax, rax);
                je(exitSkip);
                cmp(byte[rax + 0x1A], 0x2B); // is TESNPC ?
                jne(exitSkip);
                mov(r8, ptr[rax + 0x158]); // load TESRace
                test(r8, r8);
                je(exitSkip);
                cmp(r14d, ptr[r8 + 0x138]); // is shield object?
                jne(exitSkip);

                mov(r8, ptr[rbp + 0x77]);
                mov(r8, ptr[r8]);
                imul(rdx, r14, 0x78);
                mov(rdx, ptr[r8 + rdx + 0x10]); // form

                mov(r8, rsi); // attachment node
                mov(r9, rdi); // object

                call(ptr[rip + callLabel]);

                jmp(ptr[rip + retnLabel]);

                L(exitSkip);
                mov(rax, ptr[rsi]);
                mov(r8b, 1);
                jmp(ptr[rip + retnSkipLabel]);

                L(retnLabel);
                dq(targetAddr + 0x12);

                L(retnSkipLabel);
                dq(targetAddr + 0x6);

                L(callLabel);
                dq(std::uintptr_t(AttachShieldNode_Hook));
            }
        };

        LogPatchBegin(__FUNCTION__);
        {
            Assembly code(m_armorAttach_a);
            ISKSE::GetBranchTrampoline().Write6Branch(m_armorAttach_a, code.get());
        }
        LogPatchEnd(__FUNCTION__);
    }

    void EngineExtensions::Patch_DisableShieldHideOnSit(
        const Config& a_config)
    {
        struct Assembly : JITASM
        {
            Assembly(
                std::uintptr_t a_targetAddr) 
                :
                JITASM(ISKSE::GetLocalTrampoline())
            {
                Xbyak::Label exitContinue;
                Xbyak::Label exitSkip;
                Xbyak::Label callLabel;

                Xbyak::Label skip;
                Xbyak::Label cont;

                test(r8b, r8b); // bool: true = hide, false = show
                je(cont);

                push(rcx);
                push(rdx);
                push(r8);
                sub(rsp, 0x20);
                
                call(ptr[rip + callLabel]);
                
                add(rsp, 0x20);
                pop(r8);
                pop(rdx);
                pop(rcx);

                test(al, al);
                jne(skip);

                L(cont);
                mov(r9, ptr[rdx]); // the actor biped object storage thing (holds bone tree, items, nodes, ...) (BipedModel::Biped)
                mov(rax, ptr[rcx + 0x1F0]); // TESRace*
                jmp(ptr[rip + exitContinue]);

                L(skip);
                jmp(ptr[rip + exitSkip]);

                L(exitContinue);
                dq(a_targetAddr + 0xA);

                L(exitSkip);
                dq(a_targetAddr + 0x53);

                L(callLabel);
                dq(std::uintptr_t(ShouldBlockShieldHide));

            }
        };
        
        LogPatchBegin(__FUNCTION__);
        {
            Assembly code(m_hideShield_a);
            ISKSE::GetBranchTrampoline().Write6Branch(m_hideShield_a, code.get());
        }
        LogPatchEnd(__FUNCTION__);
    }

    bool EngineExtensions::Hook_TESObjectWEAP_SetEquipSlot()
    {
        bool result = VTable::Detour2(m_vtbl_TESObjectWEAP, 0x86 + 0x5, TESObjectWEAP_SetEquipSlot_Hook, std::addressof(m_TESObjectWEAP_SetEquipSlot_o));
        if (result) {
            Message("[Hook] TESObjectWEAP::SetEquipSlot");
        }
        else {
            Error("%s: FAILED", __FUNCTION__);
        }
        return result;
    }

    bool EngineExtensions::Patch_ShieldHandWorkaround()
    {
        if (!Hook::GetDst5<0xE8>(
            m_unk140609D50_BShkbAnimationGraph_SetGraphVariableInt_a,
            m_BShkbAnimationGraph_SetGraphVariableInt_o))
        {
            return false;
        }

        if (!Hook::GetDst5<0xE8>(
            m_unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_a,
            m_unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_o))
        {
            return false;
        }

        if (!Hook::GetDst5<0xE8>(
            m_unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_a,
            m_unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_o))
        {
            return false;
        }

        enum class HookTarget
        {
            k1,
            k2,
            k3
        };

        struct Assembly : JITASM
        {
            Assembly(HookTarget a_target) :
                JITASM(ISKSE::GetLocalTrampoline())
            {
                Xbyak::Label retnLabel;
                Xbyak::Label callLabel;

                std::uintptr_t targetAddr;
                std::uintptr_t callAddr;

                switch (a_target)
                {
                case HookTarget::k1:
                    targetAddr = m_unk140609D50_BShkbAnimationGraph_SetGraphVariableInt_a;
                    callAddr = std::uintptr_t(Unk140609D50_BShkbAnimationGraph_SetGraphVariableInt_Hook);
                    lea(r9, ptr[rbp - 0x38]);
                    break;
                case HookTarget::k2:
                    targetAddr = m_unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_a;
                    callAddr = std::uintptr_t(Unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_Hook);
                    mov(r9, rsi);
                    break;
                case HookTarget::k3:
                    targetAddr = m_unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_a;
                    callAddr = std::uintptr_t(Unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_Hook);
                    lea(r9, ptr[r15 - 0xB8]);
                    break;
                }

                call(ptr[rip + callLabel]);
                jmp(ptr[rip + retnLabel]);

                L(retnLabel);
                dq(targetAddr + 0x5);

                L(callLabel);
                dq(callAddr);

            }
        };

        LogPatchBegin("Unk140609D50");
        {
            Assembly code(HookTarget::k1);
            ISKSE::GetBranchTrampoline().Write5Branch(
                m_unk140609D50_BShkbAnimationGraph_SetGraphVariableInt_a, code.get());
        }
        LogPatchEnd("Unk140609D50");

        LogPatchBegin("Unk1406097C0");
        {
            Assembly code(HookTarget::k2);
            ISKSE::GetBranchTrampoline().Write5Branch(
                m_unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_a, code.get());
        }
        LogPatchEnd("Unk1406097C0");

        LogPatchBegin("Unk140634D20");
        {
            Assembly code(HookTarget::k3);
            ISKSE::GetBranchTrampoline().Write5Branch(
                m_unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_a, code.get());
        }
        LogPatchEnd("Unk140634D20");

        return true;
    }

    bool EngineExtensions::Unk140609D50_BShkbAnimationGraph_SetGraphVariableInt_Hook(
        BShkbAnimationGraph* a_graph,
        const BSFixedString& a_name,
        std::int32_t a_value,
        Actor* a_actor)
    {
        auto controller = m_Instance->m_controller.get();

        if (a_value == 10 &&
            controller->IsShieldEnabled(a_actor) &&
            (controller->GetConfig().m_shwForceIfDrawn ||
                !a_actor->actorState.IsWeaponDrawn()) &&
            Common::IsShieldEquipped(a_actor))
        {
            a_value = 0;
        }

        return m_Instance->m_BShkbAnimationGraph_SetGraphVariableInt_o(a_graph, a_name, a_value);
    }

    std::uint32_t EngineExtensions::Unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_Hook(
        IAnimationGraphManagerHolder* a_holder,
        const BSFixedString& a_name,
        std::int32_t a_value,
        Actor* a_actor)
    {
        auto controller = m_Instance->m_controller.get();

        if (a_value == 10 &&
            controller->IsShieldEnabled(a_actor) &&
            (controller->GetConfig().m_shwForceIfDrawn ||
                !a_actor->actorState.IsWeaponDrawn()) &&
            Common::IsShieldEquipped(a_actor))
        {
            a_value = 0;
        }

        return m_Instance->m_unk1406097C0_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_o(a_holder, a_name, a_value);
    }

    std::uint32_t EngineExtensions::Unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_Hook(
        IAnimationGraphManagerHolder* a_holder,
        const BSFixedString& a_name,
        std::int32_t a_value,
        Actor* a_actor)
    {
        auto controller = m_Instance->m_controller.get();

        if ((a_value == 0 || a_value == 10) &&
            controller->IsShieldEnabled(a_actor) &&
            Common::IsShieldEquipped(a_actor))
        {
            a_holder->SetVariableOnGraphsInt(
                controller->GetStringHolder()->m_iLeftHandType, a_value);
        }

        return m_Instance->m_unk140634D20_IAnimationGraphManagerHolder_SetVariableOnGraphsInt_o(a_holder, a_name, a_value);
    }

    void EngineExtensions::TESObjectWEAP_SetEquipSlot_Hook(BGSEquipType* a_this, BGSEquipSlot* a_slot)
    {
        // this just writes BGSEquipSlot* @rcx+8, but we call here on the off chance that something else hooked it
        m_Instance->m_TESObjectWEAP_SetEquipSlot_o(a_this, a_slot);

        OnSetEquipSlot evn;
        m_Instance->m_dispatchers.m_setEquipSlot.SendEvent(evn);
    }

    void EngineExtensions::CreateWeaponNodes_Hook(
        TESObjectREFR* a_actor,
        TESForm* a_object,
        bool a_left)
    {
        m_Instance->m_createWeaponNodes_o(a_actor, a_object, a_left);

        CreateWeaponNodesEvent evn{ a_actor, a_object,  a_left };
        m_Instance->m_dispatchers.m_createWeaponNodes.SendEvent(evn);
    }

    NiNode* EngineExtensions::GetScbAttachmentNode_Hook(
        TESObjectREFR* a_actor,
        TESForm* a_form,
        NiNode* a_attachmentNode)
    {
        if (!a_actor || !a_form) {
            return nullptr;
        }

        if (a_actor->formType != Actor::kTypeID) {
            return nullptr;
        }

        if (a_form->formType != TESObjectWEAP::kTypeID) {
            return nullptr;
        }

        auto actor = static_cast<Actor*>(a_actor);

        return m_Instance->m_controller->GetScbAttachmentNode(
            actor, a_form, a_attachmentNode);
    }

    void EngineExtensions::AttachShieldNode_Hook(
        Actor* a_actor,
        TESForm* a_form, 
        NiNode* a_attachmentNode,
        NiAVObject* a_object)
    {
        if (a_form) 
        {
            if (auto node = m_Instance->m_controller->GetShieldAttachmentNode(
                a_actor, a_form, a_attachmentNode); node) 
            {
                a_attachmentNode = node;
            }
        }

        a_attachmentNode->AttachChild(a_object, true);
    }

    bool EngineExtensions::ShouldBlockShieldHide(
        Actor* a_actor)
    {
        return m_Instance->m_controller->ShouldBlockShieldHide(a_actor);
    }
}