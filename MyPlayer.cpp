/**
    This plugin can be used for common player customizations
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include "Item.h"
#include "GameGraveyard.h"
#include <memory>
#include <unordered_map>

class MyPlayer : public PlayerScript, WorldScript, ServerScript 
{
private:
    struct PlayerInfo
    {
        Player *player;
        MotionMaster *motion;
        Unit *unit;
        std::unordered_map<Opcodes, WorldPacket*> packets;
        Position lastGhostPos;
    };

public:
    uint32 m_timePassed = 0;
    std::unordered_map<uint64, PlayerInfo> m_sessions;

    MyPlayer() : PlayerScript("MyPlayer"), WorldScript("MyPlayer"), ServerScript("MyPlayer") { }
    ~MyPlayer()
    {
        for(auto& info : m_sessions)
        {
            for(auto& packet : info.second.packets)
            {
                delete packet.second;
            }
        }
    }

    void checkAura(uint32 spellId, Player *player)
    {
        if(player->HasAura(spellId) <= 0)
        {
            player->CastSpell(player, spellId, false);
        }
        else
        {
            Aura* aura = player->GetAura(spellId);
            if(aura->GetDuration() <= 10000)
            {
                player->CastSpell(player, spellId, false);
            }
        }
    }

    void findAttackableUnit(PlayerInfo &info)
    {
        bool found = false;

        const Unit::AttackerSet &attackerSet = info.player->getAttackers();
        for(const auto &attacker : attackerSet)
        {
            info.unit = attacker;
            found = true;
            break;
        }

        if(!found)
            info.unit = info.player->SelectNearbyTarget((Unit*)nullptr, 50.0f);
    }

    void handleHealing(Player *player)
    {
        if(player->HealthBelowPct(35))
        {
            if(player->getClass() == CLASS_PALADIN)
            {
                player->CastSpell(player, 639, false);
            }
        }
    }

    void OnLogin(Player *player) override 
    {
        if(sConfigMgr->GetOption<bool>("MyCustom.enableHelloWorld", false)) 
        {
            ChatHandler(player->GetSession()).SendSysMessage("Hello Module!");

            PlayerInfo info{player, player->GetMotionMaster(), nullptr, {}, {}};
            m_sessions.insert({player->GetGUID(), std::move(info)});
        }
    }

    void OnLogout(Player* player) override
    {
        if(sConfigMgr->GetOption<bool>("MyCustom.enableHelloWorld", false)) 
        {
            m_sessions.erase(m_sessions.find(player->GetGUID()));
        }
    }

    void OnPlayerReleasedGhost(Player* player) override
    {
        const auto &info = m_sessions.find(player->GetGUID());
        if(info != m_sessions.end())
        {
            const Player *player = info->second.player;
            const Position &pos = player->GetPosition();
            const GraveyardStruct *closestGrave = sGraveyard->GetClosestGraveyard(pos.m_positionX, pos.m_positionY, pos.m_positionZ, player->GetMapId(), player->GetTeamId());
            info->second.lastGhostPos = Position(closestGrave->x, closestGrave->y, closestGrave->z);
        }
    }

    void OnUpdate(uint32 diff) override 
    {
        if(sConfigMgr->GetOption<bool>("MyCustom.enableHelloWorld", false)) 
        {
            m_timePassed += diff;

            for(auto& session : m_sessions)
            {
                PlayerInfo &info = session.second;
                info.packets.clear();

                if(m_timePassed >= 5000)
                {
                    if(info.player->IsAlive())
                    {
                        if(info.player->getClass() == CLASS_PALADIN)
                        {
                            if(info.player->HasAura(465) <= 0) // Devotion Aura
                            {
                                info.player->CastSpell(info.player, 465, false);
                            }
                            checkAura(19740, info.player);   // Blessing of Might
                            checkAura(21084, info.player);   // Seal of Righteousness
                        }

                        if(info.unit == nullptr)
                            findAttackableUnit(info);

                        if(info.unit != nullptr && !info.player->IsInRange2d(info.unit->m_positionX, info.unit->m_positionY, 0.0f, 2.0f))
                        {
                            Position pos = info.unit->GetPosition();
                            float z = pos.m_positionZ;
                            info.player->UpdateGroundPositionZ(pos.m_positionX - 1, pos.m_positionY - 1, z);
                            
                            info.motion->Clear();
                            info.motion->MovePoint(info.player->GetMapId(), pos.m_positionX - 1, pos.m_positionY - 1, pos.m_positionZ);

                            const Unit::AttackerSet &attackerSet = info.unit->getAttackers();
                            if(attackerSet.find(info.player) == attackerSet.end())
                                info.player->Attack(info.unit, true);
                        }
                    }
                    else if(info.player->isDead())
                    {
                        // Checking to see if stuck at a graveyard, in a building etc.
                        if(info.player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)
                            && info.player->IsInRange2d(info.lastGhostPos.m_positionX, info.lastGhostPos.m_positionY, 0.0f, 10.0f))
                        {
                            info.player->RepopAtGraveyard();
                        }
                    }

                    m_timePassed = 0;
                }

                if(m_timePassed >= 500 && info.player != nullptr && info.player->IsAlive())
                {
                    handleHealing(info.player);

                    if(info.unit != nullptr && info.unit->isDead())
                        info.unit = nullptr;

                    if(info.unit != nullptr)
                    {
                        if(info.player->getClass() == CLASS_PALADIN)
                        {
                            info.player->CastSpell(info.unit, 20271, false);
                        }
                    }

                    if(info.unit == nullptr)
                        findAttackableUnit(info);

                    if(info.player->IsStopped() && info.unit != nullptr)
                    {
                        float angle = info.player->GetAngle(info.unit->m_positionX, info.unit->m_positionY);

                        info.player->Attack(info.unit, true);

                        if(!info.player->isInFront(info.unit, M_PI_4))
                            info.player->SetFacingTo(angle);
                    }
                }

                if(info.player != nullptr && info.player->isDead())
                {
                    if(info.unit != nullptr)
                        info.unit = nullptr;

                    if(!info.player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                    {
                        info.player->CombatStop(true);
                        if(info.packets.find(CMSG_REPOP_REQUEST) == info.packets.end())
                        {
                            WorldPacket *packet = new WorldPacket(CMSG_REPOP_REQUEST);
                            packet->resize(1);
                            info.player->GetSession()->QueuePacket(packet);
                            info.packets.insert(std::make_pair(CMSG_REPOP_REQUEST, std::move(packet)));
                        }
                    }

                    // if(info.player->GetCorpse() != nullptr)
                    // {
                    //     Position pos = info.player->GetCorpse()->GetPosition();

                    //     if(!info.player->IsInRange2d(pos.m_positionX, pos.m_positionY, 0.0f, 2.0f))
                    //     {
                    //         float z = pos.m_positionZ;
                    //         info.player->UpdateGroundPositionZ(pos.m_positionX, pos.m_positionY, z);
                    //         info.motion->Clear();
                    //         info.motion->MovePoint(info.player->GetMapId(), pos.m_positionX, pos.m_positionY, z);
                    //     }
                    //     else if(info.packets.find(CMSG_RECLAIM_CORPSE) == info.packets.end())
                    //     {
                    //         WorldPacket *packet = new WorldPacket(CMSG_RECLAIM_CORPSE);
                    //         packet->resize(8);
                    //         info.player->GetSession()->QueuePacket(packet);
                    //         info.packets.insert(std::make_pair(CMSG_RECLAIM_CORPSE, std::move(packet)));
                    //     }
                    // }
                }
            }
        }
    }
};

void AddMyPlayerScripts()
{
    if(sConfigMgr->GetOption<bool>("MyCustom.enableHelloWorld", false))
    {
        new MyPlayer();
    }
}

