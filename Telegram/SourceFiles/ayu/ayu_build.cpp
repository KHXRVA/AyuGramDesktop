// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_build.h"

#include "ayu_build_config.h"

namespace AyuBuild {

bool IsCommunity() {
	return AYU_COMMUNITY_BUILD != 0;
}

QString Tag() {
	return IsCommunity() ? QString::fromUtf8(AYU_BUILD_TAG) : QString();
}

QString Author() {
	return IsCommunity() ? QString::fromUtf8(AYU_BUILD_AUTHOR) : QString();
}

QString AuthorLink() {
	return IsCommunity() ? QString::fromUtf8(AYU_BUILD_AUTHOR_LINK) : QString();
}

QString CommunityLabel() {
	return IsCommunity()
		? Author() + QString::fromUtf8(" for AyuGram community")
		: QString();
}

QString VersionSuffix() {
	return IsCommunity() ? QString::fromUtf8(" (") + Tag() + ')' : QString();
}

} // namespace AyuBuild
