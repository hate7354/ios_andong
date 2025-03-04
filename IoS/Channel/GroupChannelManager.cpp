#include "pch.h"

#include "GroupChannelManager.h"

GroupChannelManager::GroupChannelManager(int groups)
{
	if (groups > 0)
		_groupChannels.resize(groups);
}

GroupChannelManager::~GroupChannelManager()
{
}

void GroupChannelManager::addGroup(GroupChannelInfo& channelGroup)
{
	_groupChannels.push_back(channelGroup);
}

void GroupChannelManager::delGroup(int group)
{
	if (group < 0 || group >= _groupChannels.size())
		return;

	if (_groupChannels[group].getCount() <= 0)
		_groupChannels.erase(_groupChannels.begin() + group);
}

int GroupChannelManager::GetFrame(int group) const
{
	if (group < 0 || group >= _groupChannels.size())
		return 0;

	return _groupChannels[group].getFrame();
}

GroupChannelInfo* GroupChannelManager::getGroup(int group)
{
	if (group < 0 || group >= _groupChannels.size())
		return nullptr;

	return &_groupChannels[group];
}