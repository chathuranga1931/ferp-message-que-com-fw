/// Vendor Modules
import { Cash } from 'cash-dom';

/// AGI-Drive Modules
import { Alert } from '../components/alert';
import { Page } from '../services/router/page';
import { Context } from '../utils/context';
import { Fetch } from '../utils/fetch';

/** Config Page Implementation. */
class _Config_impl extends Page {
    /****************
     *  PROPERTIES  *
     ****************/

    /** Base User Page Title. */
    readonly title = 'WiFi Configuration';

    /** Base HREF Label. */
    readonly label = 'config';

    /******************
     *  INHERITABLES  *
     ******************/

    protected async m_onAttach() {
        this.m_trigger(); // pre-trigger all inputs
    }

    /********************
     *  EVENT HANDLERS  *
     ********************/

    /**
     * Coordinates handling user-page actions.
     * @param action                                Action to handle.
     * @param self
     */
    protected override m_onAction(action: `user.${'save' | 'connect'}`, self: Cash) {
        if (action === 'user.save') return this.m_saveInputs('user.save', this.$inputs);
        if (action === 'user.connect') return this.m_attemptConnection();
    }

    /** Coordinates attempting a device connection. */
    private async m_attemptConnection() {
        Fetch.request('user.stamode').then((res) =>
            res.match({
                okay: () => Alert.create({ title: 'Connected Successfully', context: Context.SUCCESS }),
                error: () => Alert.create({ title: 'Failed to Connect', context: Context.DANGER }),
            })
        );
    }
}

/// Singleton Instance.
export const _Config_page = new _Config_impl();
