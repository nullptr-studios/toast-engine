#include "asset_params.hpp"

namespace assets {

auto assetParams() -> AssetParams& {
	static AssetParams instance;
	return instance;
}

}
