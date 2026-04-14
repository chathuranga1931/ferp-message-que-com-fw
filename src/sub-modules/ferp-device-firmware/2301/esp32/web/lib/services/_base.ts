/** Service Abstractions. */
export namespace Service {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Service Interface. */
    export interface IBase {
        init(): Promise<void>;
    }

    /** Service Interface Constructor. */
    export type From<K extends keyof IBase> = Pick<IBase, K>;
}
