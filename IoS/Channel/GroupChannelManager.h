#pragma once

#include <vector>
#include "GroupChannelInfo.h"

class GroupChannelManager {
private:
	std::vector<GroupChannelInfo> _groupChannels;

public:
	GroupChannelManager(int groups = 0);
	~GroupChannelManager();
	void addGroup(GroupChannelInfo& channelGroup);
	void delGroup(int group);
	int GetFrame(int group) const;
	GroupChannelInfo* getGroup(int group);
};
