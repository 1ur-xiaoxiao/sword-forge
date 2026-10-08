/* 仅供验收：创建合法独立存档，找到下一锤会产生指定品质的随机种子。
   正式游戏没有调试奖励或指定品质接口。 */
#include "game.h"
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 3) return 1;
    int rarity = atoi(argv[2]);
    for (uint32_t seed=1; seed<100000; ++seed) {
        Game g, probe; int index;
        game_init(&g, seed); g.level=20; g.smith_level=10; g.gold=10000;
        g.hp=game_max_hp(&g);
        for (int i=0;i<MATERIAL_COUNT;++i)g.materials[i]=1000;
        for (int i=0;i<ITEM_COUNT;++i)g.items[i]=30;
        g.weapon_count=4; g.next_id=5; g.forged=3;
        for(int i=0;i<4;++i)g.weapons[i]=(Weapon){i+1,i,i,recipes[i].attack+i*9,0};
        probe=g;
        if(forge_weapon(&probe,5,1,&index)==OK && probe.weapons[index].rarity==rarity)
            return save_game(&g,argv[1])?0:2;
    }
    return 3;
}
