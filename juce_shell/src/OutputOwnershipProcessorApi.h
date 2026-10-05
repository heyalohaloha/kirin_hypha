// Class-scope declarations included by PluginProcessor.h; not a standalone header.
// POST の出力を取る活動を、今の状態で始めてよいか（OutputOwnership.h の表）。どの入口もこれで決める。
hypha::output_owner::States outputStates() const;
hypha::output_owner::Decision outputDecision (hypha::output_owner::Activity) const;
// Reference の外の状態（形式・書き出し・bypass・live 比較・PRE/POST Blind・Keep／Record）。Reference の controller が
// 自分の入口で聞く。
hypha::output_owner::States externalOutputStates() const;
