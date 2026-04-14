/** Maybe Monads. */
export namespace Maybe {
    /**************
     *  TYPEDEFS  *
     **************/

    /** Available Maybe Outcomes. */
    export type Outcome = 'some' | 'none';

    /** Maybe Mapping. */
    export interface IMap<T> {
        some: ISome<T>;
        none: INone<T>;
    }

    /** Pattern Matcher Handlers. */
    export interface IPattern<T, U> {
        readonly some: (value: T) => U;
        readonly none: () => U;
    }

    /** Maybe Monad Interface. */
    export interface IMaybe<T> {
        kind: Outcome;
        is<O extends Outcome>(kind: O): this is IMap<T>[O];
        unwrap(opt?: T): T;
        map<U>(fn: (value: T) => U): IMaybe<U>;
        match<U>(fn: IPattern<T, U>): U;
    }

    /** Maybe Some Result. */
    export interface ISome<T> extends IMaybe<T> {
        kind: 'some';
        unwrap(): T;
        map<U>(fn: (value: T) => U): ISome<U>;
    }

    /** Maybe None Result. */
    export interface INone<T> extends IMaybe<T> {
        kind: 'none';
        unwrap(opt?: T): undefined extends T ? never : T;
        map<U>(fn: (value: T) => U): INone<U>;
    }

    /***************
     *  FACTORIES  *
     ***************/

    /**
     * Constructs a `Some` maybe result.
     * @param value                 Value to set.
     */
    export const Some = <T>(value: T): ISome<T> => ({
        kind: 'some',
        is: (kind) => kind === 'some',
        unwrap: () => value,
        map: (fn) => Some(fn(value)),
        match: (fn) => fn.some(value),
    });

    /**
     * Constructs a `None` maybe result.
     * @param message               Optional error message.
     */
    export const None = <T>(message?: string): INone<T> => ({
        kind: 'none',
        is: (kind) => kind === 'none',
        match: (fn) => fn.none(),
        map: () => None(message),
        unwrap: <any>((opt?: T) => {
            if (opt === undefined) throw new Error(message ?? 'Monad::Maybe | Cannot unwrap none!');
            return opt;
        }),
    });
}
