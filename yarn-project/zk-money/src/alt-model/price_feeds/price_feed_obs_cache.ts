import type { Provider } from '@ethersproject/providers';
import { LazyInitCacheMap } from '../../app/util/lazy_init_cache_map.js';
import { createAssetPriceObs, PriceObs } from './price_fetchers.js';
import { UnderlyingAmountPollerCache } from '../../alt-model/defi/bridge_data_adaptors/caches/underlying_amount_poller_cache.js';
import { ChainLinkPollerCache } from './chain_link_poller_cache.js';

export function createPriceFeedObsCache(
  web3Provider: Provider,
  chainLinkPollerCache: ChainLinkPollerCache,
  underlyingAmountPollerCache: UnderlyingAmountPollerCache,
) {
  const priceFeedObsCache: PriceFeedObsCache = new LazyInitCacheMap((assetAddress: string) =>
    createAssetPriceObs(assetAddress, web3Provider, chainLinkPollerCache, underlyingAmountPollerCache, getPriceFeedObs),
  );
  const getPriceFeedObs = priceFeedObsCache.get.bind(priceFeedObsCache);
  return priceFeedObsCache;
}

export type PriceFeedObsCache = LazyInitCacheMap<string, PriceObs | undefined>;