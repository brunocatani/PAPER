#include "attachment_bindings/AttachmentBindingPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace paper::attachment_bindings::policy;
namespace
{
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    struct Node { std::string_view name; std::vector<Node*> children; };
    auto find(Node* root, std::string_view name) {
        return findUnique(root,name,[](Node* n){return n->name;},
            [](Node* n){return n->children.size();},[](Node* n,std::size_t i){return n->children[i];});
    }
}
int main()
{
    try {
        const auto glock=parseNodeRemap("remapNode_SightsGlock19x_P-Glock19xSights");
        require(glock&&glock->node=="SightsGlock19x"&&glock->connection=="P-Glock19xSights","Declared Glock remap lost its arguments");
        require(parseNodeRemap("remapNode_CFrontSight_P-FrontSight").has_value(),"Ordinary named-node remap rejected");
        for(auto text:{"", "remapNode", "remapNode__P-Sight", "remapNode_Sight_", "remapNode_Sight_P-",
            "remapNode_Sight_WithUnderscore_P-Sight", "remapNode_Sight_P-Sight_Extra", "remapNode_P-Laser_P-Mount", "hideNode_Sight_P-Sight"})
            require(!parseNodeRemap(text),"Unrelated, ambiguous or connection-only function accepted");
        Node sights{"SightsGlock19x",{}}, mount{"Mount",{}}, scope{"Scope",{&sights,&mount}}, bolt{"WeaponBolt",{}}, root{"Weapon",{&bolt,&scope}};
        const auto first=find(&root,glock->node);require(first.complete&&!first.ambiguous&&first.match==&sights,"Unique scoped branch not found");
        Node second{"SightsGlock19x",{}};root.children.push_back(&second);
        require(find(&root,glock->node).ambiguous,"Duplicate nodes chose the first match");root.children.pop_back();
        require(!find(&root,"Missing").match,"Missing node was fabricated");
        scope.children.push_back(&root);require(!find(&root,glock->node).complete,"Cyclic hierarchy did not stop");scope.children.pop_back();
        root.children.resize(513,nullptr);require(!find(&root,glock->node).complete,"Oversized hierarchy exceeded traversal storage");
        Identity current{1,2,3,4,5,6};require(current.valid()&&current==Identity{1,2,3,4,5,6},"Stable scene identity changed");
        for(auto changed:{Identity{7,2,3,4,5,6},Identity{1,7,3,4,5,6},Identity{1,2,7,4,5,6},Identity{1,2,3,7,5,6},Identity{1,2,3,4,7,6},Identity{1,2,3,4,5,7}})
            require(current!=changed,"Replacement scene/instance/generation reused the previous repair pass");
        require(!Identity{}.valid()&&!Identity{1,2,0,4,5,6}.valid(),"Missing weapon identity accepted");
        std::cout<<"PASS attachment remap arguments, unique scoped branch, ambiguity/cycle/budget rejection and scene identity invalidation\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; }
}
