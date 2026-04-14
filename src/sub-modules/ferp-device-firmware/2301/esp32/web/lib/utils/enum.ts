/** Enumeration Functionality. */
export namespace Enum {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Generic Enum Alias. */
    export type Generic<T> = {
        [id: string]: T | string;
        [num: number]: string;
    };

    /********************
     *  PUBLIC METHODS  *
     ********************/

    /**
     * Gets the label for a given value of an enum.
     * @param value                         Value to get label of.
     * @param _enum_impl                    Base enumeration.
     */
    export const stringify = <E extends Generic<unknown>>(value: E[string], _enum_impl: E) => _enum_impl[value as any];

    /**
     * Gets the keys of an enumeration.
     * @param _enum_impl                    Base enumeration.
     */
    export const keys = <E extends Generic<unknown>>(_enum_impl: E): E[number][] =>
        Object.keys(_enum_impl).filter((key) => isNaN(parseInt(key, 10)));

    /**
     * Coordinates mapping an enumeration.
     * @param _enum_impl                    Base enumeration.
     * @param callback                      Map callback.
     */
    export const map = <R, E extends Generic<unknown> = Generic<unknown>>(
        _enum_impl: E,
        callback: Utils_t.Functor.Any<R, [key: E[number], value: E[string]]>
    ) => keys(_enum_impl).map((key) => callback(key, _enum_impl[key]));
}
