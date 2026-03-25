#include <cstdint>
#include <cstring>
#include <cmath>
#include <format>
#include <string>
#include <miniAPI.h>
#include <nise/stub.h>
#include <memscan.h>
#include <inlinehook.h>

#include "nbt/nbt.h"
#include "item/item.h"
#include "item/itemstackbase.h"
#include "item/IFoodItemComponent.h"

static void* g_Item_appendHover_orig = nullptr;
using Item_appendHover_t = void(*)(void*, ItemStackBase*, void*, std::string&, bool);

std::string buildBarString(int v, const std::string full, const std::string half) {
    std::string out;
    for (int i = 0; i < (int)v; i+=2) out += full; // 
    if (fmod(v, 2) != 0)
        out += half; // 
    return out;
}

void FoodTooltips(IFoodItemComponent* food, std::string& text) {
    int nutrition = food->getNutrition();
    int saturation = food->getSaturationModifier() * nutrition * 2;
    if(nutrition > 0) text += std::format("\n{} ({})", buildBarString(nutrition, "", ""), nutrition);
    if(saturation > 0) text += std::format("\n{} ({})", buildBarString(saturation, "", ""), saturation);
}

void BeeNest(ItemStackBase* stack, std::string& text) {
    void* data = stack->mUserData;
    if(!data || !containsTag(data, "Occupants") && !containsTag(data, "occupants")) {
      text += "\n§7Contains 0 bees§r";
      return;
    };
    auto* list = reinterpret_cast<ListTagLayout*>(getListTag(data, "Occupants"));
    if (!list)
        list = reinterpret_cast<ListTagLayout*>(getListTag(data, "occupants"));
    if(list) {
        int size = listSize(list);
        text += std::format("\n§7Contains {} bee{}§r", size, size > 1 ? "s" : "");
    }
}

void ToolDurability(short maxDamage, ItemStackBase* s, std::string& text) {
    short current = maxDamage - ItemStackBase_getDamageValue(s);
    text += std::format("\n§7Durability: {} / {}§r",  current, maxDamage);
}

static void Item_appendFormattedHovertext_hook(void* self, ItemStackBase* stack, void* level, std::string& text, bool flag) {
    text += "Berhasil\n";
    if (g_Item_appendHover_orig) ((Item_appendHover_t)g_Item_appendHover_orig)(self, stack, level, text, flag);
    text += "\nBerhasil";
    
    Item* item = stack->mItem.get();
    short maxDamage = item->getMaxDamage();
    IFoodItemComponent* food = item->getFood();
    std::string rawNameId = item->mRawNameId.mStr;
    
    if(item->isFood() && food != nullptr) FoodTooltips(food, text); 
  	if(maxDamage != 0) ToolDurability(maxDamage,stack,text);
  	if(rawNameId == "bee_nest" || rawNameId == "beehive") BeeNest(stack, text);
  
  	text += std::format("\n§7{}:{} (#{})§r", item->mNamespace, rawNameId, item->mId);
}

void* resolve(const char *sgig, const char *name) {
    sigscan_handle *handle = sigscan_setup(sgig, "libminecraftpe.so", GPWN_SIGSCAN_XMEM);
    if(!handle) return (void*)0;
    
    void *func = get_sigscan_result(handle);
    
    sigscan_cleanup(handle);
    
    if(func == (void*) -1) return (void*)0;
    return func;
}

__attribute__((constructor))
static void mod_init() {
    GlossInit(true);

    Nbt_treeFind = (Nbt_treeFind_t)resolve("?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 FD 03 00 91 F3 03 00 AA ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? A9 F5 03 13 AA ?? ?? ?? 14 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? 39 ?? ?? ?? 36 ?? ?? ?? F9 ?? ?? ?? 36 ?? ?? ?? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 34 ?? ?? ?? 37 ?? ?? ?? 52 ?? ?? ?? 71 ?? ?? ?? 54 ?? ?? ?? 14 ?? ?? ?? 91 ?? ?? ?? 37 ?? ?? ?? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ?? ?? ?? 94 ?? ?? ?? 35 DF 02 18 EB ?? ?? ?? 54 E8 03 1F 2A ?? ?? ?? 71 ?? ?? ?? 54 F5 03 17 AA ?? ?? ?? F9 ?? ?? ?? B5 ?? ?? ?? 14 ?? ?? ?? 54 ?? ?? ?? 17 BF 02 13 EB ?? ?? ?? 54 ?? ?? ?? 39 ?? ?? ?? A9 E0 03 14 AA ?? ?? ?? D3 ?? ?? ?? 72 ?? ?? ?? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ?? ?? ?? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A8 C0 03 5F D6 ?? ?? ?? A9","Nbt_treeFind");
        
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)resolve("?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 ?? ?? ?? D5 ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? F9 ?? ?? ?? B4","ItemStackBase_getDamageValue");
    
    miniAPI::hook::vtable("libminecraftpe.so","4Item",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    
    miniAPI::hook::vtable("libminecraftpe.so","9BrushItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","17FlintAndSteelItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","14FishingRodItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","12CrossbowItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","7BowItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","9BlockItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","18CarrotOnAStickItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11TridentItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShovelItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShieldItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10ShearsItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","10DiggerItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","7HoeItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","11PickaxeItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
    miniAPI::hook::vtable("libminecraftpe.so","8MaceItem",55,&g_Item_appendHover_orig,(void*)Item_appendFormattedHovertext_hook);
}
