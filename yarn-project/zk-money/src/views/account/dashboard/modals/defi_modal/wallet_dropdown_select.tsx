import { useApp } from '../../../../../alt-model/index.js';
import { WalletId, wallets } from '../../../../../app/index.js';
import { LegacySelect } from '../../../../../components/index.js';
import style from './wallet_dropdown_select.module.scss';

const ITEMS = (window.ethereum ? wallets : wallets.filter(w => w.id !== WalletId.METAMASK)).map(wallet => ({
  id: wallet.id,
  content: wallet.nameShort,
}));

export function WalletDropdownSelect() {
  const { userSession } = useApp();
  return (
    <LegacySelect
      className={style.select}
      items={ITEMS}
      onSelect={id => userSession?.changeWallet(id)}
      trigger={<div className={style.trigger}>Connect a wallet</div>}
    />
  );
}